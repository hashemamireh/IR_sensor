#include "menu.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <ctime>

namespace ui {

// Layout: yellow status bar 0–15, title 16–22, rule at 24, 4 rows from 27
static constexpr int TITLE_Y      = 16;
static constexpr int RULE_Y       = 24;
static constexpr int FIRST_ROW_Y  = 27;
static constexpr int ROW_PITCH    = 9;
static constexpr int VISIBLE_ROWS = 4;

static constexpr const char* AP_IP = "192.168.4.1";
static constexpr uint32_t RECORD_DURATIONS_S[] = {5, 10, 30, 60, 120, 300};

// ── Formatting ──────────────────────────────────────────────────────────────

static std::string fmt(const char* f, ...) {
    char buf[64];
    va_list args;
    va_start(args, f);
    vsnprintf(buf, sizeof(buf), f, args);
    va_end(args);
    return buf;
}

static unsigned u(uint64_t v) { return static_cast<unsigned>(v); }

static std::string rate_label(uint32_t hz) { return fmt("%ukHz", u(hz / 1000)); }

static std::string duration_label(uint32_t s) {
    if (s >= 60 && s % 60 == 0) return fmt("%umin", u(s / 60));
    return fmt("%us", u(s));
}

static std::string timeout_label(uint32_t s) { return s == 0 ? "Never" : duration_label(s); }

static const char* brightness_label(settings::Brightness b) {
    switch (b) {
        case settings::Brightness::Low:    return "Low";
        case settings::Brightness::Medium: return "Medium";
        case settings::Brightness::High:   return "High";
    }
    return "?";
}

// One decimal place, e.g. 15234567 bytes → "15.2"
static std::string mb(uint64_t bytes) {
    uint64_t tenths = bytes / 100000;
    return fmt("%u.%u", u(tenths / 10), u(tenths % 10));
}

static bool to_utc(uint64_t utc_us, tm* out) {
    if (!api::is_plausible_utc_us(static_cast<int64_t>(utc_us))) return false;
    time_t t = static_cast<time_t>(utc_us / 1000000);
    return gmtime_r(&t, out) != nullptr;
}

static std::string short_date(uint64_t utc_us) {
    tm t;
    if (!to_utc(utc_us, &t)) return "--";
    return fmt("%02d-%02d %02d:%02d", t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
}

static std::vector<std::string> info_lines(const RecordingSummary& r) {
    const irec::Header& h = r.header;
    tm t;
    bool dated = to_utc(h.start_utc_us, &t);
    uint64_t ms = irec::duration_ms(h);
    return {
        dated ? fmt("Date: %04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday) : "Date: clock not set",
        dated ? fmt("Time: %02d:%02d:%02d UTC", t.tm_hour, t.tm_min, t.tm_sec) : "Time: --",
        fmt("Length: %u.%u s", u(ms / 1000), u(ms % 1000 / 100)),
        fmt("Rate: %u.%u Hz", u(h.sample_rate_mhz / 1000), u(h.sample_rate_mhz % 1000 / 100)),
        fmt("Samples: %u", u(h.sample_count)),
        fmt("Size: %u KB", u((r.size_bytes + 1023) / 1024)),
        fmt("Compressed: %s", (h.flags & irec::FLAG_COMPRESSED) ? "yes" : "no"),
        fmt("Overrun: %s", (h.flags & irec::FLAG_OVERRUN) ? "YES" : "no"),
        (h.atten_tenths_db % 10) ? fmt("Atten: %u.%u dB", u(h.atten_tenths_db / 10), u(h.atten_tenths_db % 10))
                                 : fmt("Atten: %u dB", u(h.atten_tenths_db / 10)),
    };
}

// ── Stack ───────────────────────────────────────────────────────────────────

Menu::Menu(Device& dev) : dev_(dev) {
    settings::Settings s = dev_.settings();
    rec_rate_hz_    = s.default_rate_hz;
    rec_duration_s_ = s.default_duration_s;
    last_state_     = dev_.status().state;
    reset_home();
}

void Menu::clamp_selection(Screen& s) {
    int n = static_cast<int>(s.items.size());
    s.selected = std::max(0, std::min(s.selected, n - 1));
    if (s.selected < s.top) s.top = s.selected;
    if (s.selected >= s.top + VISIBLE_ROWS) s.top = s.selected - VISIBLE_ROWS + 1;
    int max_top = std::max(0, std::max(n, static_cast<int>(s.lines.size())) - VISIBLE_ROWS);
    s.top = std::max(0, std::min(s.top, max_top));
}

void Menu::push(Screen s) {
    if (s.build) s.build(s);
    clamp_selection(s);
    stack_.push_back(std::move(s));
    dirty_ = true;
}

void Menu::pop() {
    if (stack_.size() > 1) stack_.pop_back();
    rebuild_top();
}

void Menu::rebuild_top() {
    Screen& s = stack_.back();
    if (s.build) s.build(s);
    clamp_selection(s);
    dirty_ = true;
}

void Menu::reset_home() {
    stack_.clear();
    sleep_warning_ = false;
    push(home_screen());
}

void Menu::move(Screen& s, int delta) {
    s.selected += delta;
    clamp_selection(s);
}

void Menu::message(const std::string& title, std::vector<std::string> lines) {
    Screen s;
    s.kind  = Kind::Text;
    s.title = title;
    s.lines = std::move(lines);
    s.lines.push_back("");
    s.lines.push_back("Press to continue");
    push(std::move(s));
}

bool Menu::frozen() const { return stack_.back().kind == Kind::Recording; }

// ── Input ───────────────────────────────────────────────────────────────────

void Menu::handle(Input in) {
    Screen& s = stack_.back();
    dirty_ = true;

    switch (s.kind) {
        case Kind::Home:
            if (in == Input::Press) push(main_menu());
            return;

        case Kind::Recording:
            if (in == Input::Press || in == Input::LongPress) dev_.stop_recording();
            return;

        case Kind::Saving:
            return;

        case Kind::Text:
            if (sleep_warning_) {  // any input means "stay awake"
                dismiss_sleep_warning();
                return;
            }
            if (in == Input::Left || in == Input::Right) {
                s.top += (in == Input::Right) ? 1 : -1;
                clamp_selection(s);
            } else {
                pop();
            }
            return;

        case Kind::List:
            if (in == Input::Left) {
                move(s, -1);
            } else if (in == Input::Right) {
                move(s, +1);
            } else if (in == Input::LongPress) {
                pop();
            } else if (!s.items.empty()) {
                // Copy first: the action may pop (destroy) this screen
                std::function<void()> action = s.items[s.selected].action;
                if (action) action();
            }
            return;
    }
}

void Menu::tick() {
    api::DeviceState now = dev_.status().state;
    if (now == last_state_) return;
    api::DeviceState before = last_state_;
    last_state_ = now;

    switch (now) {
        case api::DeviceState::Recording:
            reset_home();
            push(recording_screen());
            break;
        case api::DeviceState::Saving:
            reset_home();
            push(saving_screen());
            break;
        case api::DeviceState::Idle:
            if (before == api::DeviceState::Recording || before == api::DeviceState::Saving) {
                reset_home();  // home shows the result summary
            } else {
                rebuild_top();
            }
            break;
        case api::DeviceState::Compressing:
            dirty_ = true;
            break;
    }
}

// ── Screens ─────────────────────────────────────────────────────────────────

Menu::Screen Menu::home_screen() {
    Screen s;
    s.kind = Kind::Home;
    return s;
}

Menu::Screen Menu::main_menu() {
    Screen s;
    s.title = "Menu";
    s.items = {
        {"Record",     [this] { push(record_menu()); }},
        {"Recordings", [this] { push(recordings_menu()); }},
        {"WiFi",       [this] { push(wifi_menu()); }},
        {"Storage",    [this] { push(storage_menu()); }},
        {"Settings",   [this] { push(settings_menu()); }},
        {"Deep Sleep", [this] {
            push(confirm("Deep sleep?", [this] {
                if (!dev_.deep_sleep()) message("Device busy", {"Try again shortly"});
            }));
        }},
    };
    return s;
}

Menu::Screen Menu::record_menu() {
    Screen s;
    s.build = [this](Screen& s) {
        s.title = "Record";
        s.items = {
            {"Start " + duration_label(rec_duration_s_) + " @ " + rate_label(rec_rate_hz_),
             [this] { start_recording(); }},
            {"Duration: " + duration_label(rec_duration_s_), [this] {
                std::vector<Option> options;
                for (uint32_t d : RECORD_DURATIONS_S) {
                    if (d <= recording::max_duration_s(rec_rate_hz_)) options.push_back({duration_label(d), d});
                }
                push(choice("Duration", options, rec_duration_s_, [this](uint32_t v) {
                    rec_duration_s_ = v;
                    rebuild_top();
                }));
            }},
            {"Rate: " + rate_label(rec_rate_hz_), [this] {
                std::vector<Option> options;
                for (uint32_t r : recording::SAMPLE_RATES_HZ) options.push_back({rate_label(r), r});
                push(choice("Sample rate", options, rec_rate_hz_, [this](uint32_t v) {
                    rec_rate_hz_    = v;
                    rec_duration_s_ = std::min(rec_duration_s_, recording::max_duration_s(v));
                    rebuild_top();
                }));
            }},
        };
    };
    return s;
}

void Menu::start_recording() {
    switch (dev_.start_recording(rec_rate_hz_, rec_duration_s_)) {
        case recording::StartResult::Ok:
            tick();  // switch to the recording screen right away
            break;
        case recording::StartResult::Busy:
            message("Device busy", {"Try again shortly"});
            break;
        case recording::StartResult::InsufficientSpace:
            message("Not enough space", {"Delete recordings", "or record shorter"});
            break;
        case recording::StartResult::InvalidRate:
        case recording::StartResult::InvalidDuration:
            message("Invalid settings");
            break;
        case recording::StartResult::StorageError:
            message("Storage error");
            break;
    }
}

Menu::Screen Menu::recordings_menu() {
    Screen s;
    s.build = [this](Screen& s) {
        std::vector<RecordingSummary> recs = dev_.recordings();
        s.title = fmt("Recordings (%u)", u(recs.size()));
        s.items.clear();
        for (auto it = recs.rbegin(); it != recs.rend(); ++it) {  // newest first
            RecordingSummary r = *it;
            uint64_t secs = (irec::duration_ms(r.header) + 500) / 1000;
            s.items.push_back({fmt("#%u %s %us", u(r.id), short_date(r.header.start_utc_us).c_str(), u(secs)),
                               [this, r] { push(recording_menu(r)); }});
        }
        if (s.items.empty()) s.items.push_back({"(none)", nullptr});
    };
    return s;
}

Menu::Screen Menu::recording_menu(const RecordingSummary& r) {
    Screen s;
    s.title = fmt("Recording #%u", u(r.id));
    const uint32_t id = r.id;
    s.items = {
        {"View Info", [this, r] { message(fmt("#%u", u(r.id)), info_lines(r)); }},
        {"Delete", [this, id] {
            push(confirm(fmt("Delete #%u?", u(id)), [this, id] {
                bool ok = dev_.delete_recording(id);
                pop();  // back to the list, rebuilt without it
                if (!ok) message("Delete failed");
            }));
        }},
    };
    return s;
}

Menu::Screen Menu::wifi_menu() {
    Screen s;
    s.build = [this](Screen& s) {
        bool on = dev_.status().wifi_enabled;
        s.title = "WiFi";
        s.items = {
            {on ? "AP: On" : "AP: Off", [this, on] {
                if (dev_.set_wifi_enabled(!on)) rebuild_top();
                else message("WiFi error");
            }},
            {"Show SSID & IP", [this] { show_wifi_info(); }},
        };
    };
    return s;
}

void Menu::show_wifi_info() {
    Screen s;
    s.kind  = Kind::Text;
    s.title = "WiFi";
    s.lines = {
        std::string("SSID: ") + dev_.ssid(),
        std::string("Pass: ") + dev_.password(),
        std::string("IP:   ") + AP_IP,
        "Press to continue",
    };
    push(std::move(s));
}

void Menu::show_sleep_warning(uint32_t seconds_left) {
    std::vector<std::string> lines = {fmt("in %u s", u(seconds_left)), "", "Press to stay awake"};
    if (sleep_warning_) {
        stack_.back().lines = std::move(lines);
        dirty_ = true;
        return;
    }
    Screen s;
    s.kind  = Kind::Text;
    s.title = "Going to sleep";
    s.lines = std::move(lines);
    push(std::move(s));
    sleep_warning_ = true;
}

void Menu::dismiss_sleep_warning() {
    if (!sleep_warning_) return;
    sleep_warning_ = false;
    pop();
}

Menu::Screen Menu::storage_menu() {
    Screen s;
    s.build = [this](Screen& s) {
        StatusSnapshot st = dev_.status();
        s.title = "Storage";
        s.items = {
            {"Used " + mb(st.total_bytes - st.free_bytes) + "/" + mb(st.total_bytes) + " MB", nullptr},
            {fmt("Recordings: %u", u(st.recording_count)), nullptr},
            {"Compress All", [this] {
                push(confirm("Compress all?", [this] {
                    int n = dev_.compress_all();
                    if (n > 0)       reset_home();  // progress is shown there
                    else if (n == 0) message("Nothing to do", {"All recordings are", "already compressed"});
                    else if (n == -1) message("Device busy", {"Try again shortly"});
                    else             message("Not available", {"See serial log"});
                }));
            }},
            {"Clear All", [this] {
                push(confirm("Delete ALL?", [this] {
                    int n = dev_.delete_all();
                    if (n < 0) message("Device busy", {"Try again shortly"});
                    else message("Deleted", {fmt("%d recordings", n)});
                }));
            }},
        };
    };
    return s;
}

Menu::Screen Menu::settings_menu() {
    Screen s;
    s.build = [this](Screen& s) {
        settings::Settings cur = dev_.settings();
        s.title = "Settings";
        s.items = {
            {std::string("Brightness: ") + brightness_label(cur.brightness), [this, cur] {
                std::vector<Option> options;
                for (auto b : {settings::Brightness::Low, settings::Brightness::Medium, settings::Brightness::High}) {
                    options.push_back({brightness_label(b), static_cast<uint32_t>(b)});
                }
                push(choice("Brightness", options, static_cast<uint32_t>(cur.brightness), [this](uint32_t v) {
                    update_settings([v](settings::Settings& s) { s.brightness = static_cast<settings::Brightness>(v); });
                }));
            }},
            {"Timeout: " + timeout_label(cur.screen_timeout_s), [this, cur] {
                std::vector<Option> options;
                for (uint32_t t : settings::SCREEN_TIMEOUTS_S) options.push_back({timeout_label(t), t});
                push(choice("Screen timeout", options, cur.screen_timeout_s, [this](uint32_t v) {
                    update_settings([v](settings::Settings& s) { s.screen_timeout_s = v; });
                }));
            }},
            {"Def. rate: " + rate_label(cur.default_rate_hz), [this, cur] {
                std::vector<Option> options;
                for (uint32_t r : recording::SAMPLE_RATES_HZ) options.push_back({rate_label(r), r});
                push(choice("Default rate", options, cur.default_rate_hz, [this](uint32_t v) {
                    update_settings([v](settings::Settings& s) { s.default_rate_hz = v; });
                }));
            }},
            {"Def. length: " + duration_label(cur.default_duration_s), [this, cur] {
                std::vector<Option> options;
                for (uint32_t d : settings::DEFAULT_DURATIONS_S) options.push_back({duration_label(d), d});
                push(choice("Default length", options, cur.default_duration_s, [this](uint32_t v) {
                    update_settings([v](settings::Settings& s) { s.default_duration_s = v; });
                }));
            }},
            {"Sleep after: " + timeout_label(cur.sleep_timeout_s), [this, cur] {
                std::vector<Option> options;
                for (uint32_t t : settings::SLEEP_TIMEOUTS_S) options.push_back({timeout_label(t), t});
                push(choice("Sleep after", options, cur.sleep_timeout_s, [this](uint32_t v) {
                    update_settings([v](settings::Settings& s) { s.sleep_timeout_s = v; });
                }));
            }},
        };
    };
    return s;
}

void Menu::update_settings(const std::function<void(settings::Settings&)>& change) {
    settings::Settings s = dev_.settings();
    change(s);
    if (dev_.save_settings(s)) rebuild_top();
    else message("Not saved", {"Device busy"});
}

Menu::Screen Menu::recording_screen() {
    Screen s;
    s.kind  = Kind::Recording;
    s.build = [this](Screen& s) {
        StatusSnapshot st = dev_.status();
        s.title = fmt("Recording #%u", u(st.recording_id));
        s.lines = {duration_label(st.total_s) + " @ " + rate_label(st.rate_hz), "", "Press to stop"};
    };
    return s;
}

Menu::Screen Menu::saving_screen() {
    Screen s;
    s.kind  = Kind::Saving;
    s.build = [this](Screen& s) {
        s.title = fmt("Saving #%u", u(dev_.status().recording_id));
        s.lines = {"Writing to flash..."};
    };
    return s;
}

Menu::Screen Menu::confirm(const std::string& question, std::function<void()> on_yes) {
    Screen s;
    s.title = question;
    s.items = {
        {"No",  [this] { pop(); }},
        {"Yes", [this, on_yes] { pop(); on_yes(); }},
    };
    return s;
}

Menu::Screen Menu::choice(const std::string& title, const std::vector<Option>& options, uint32_t current,
                          std::function<void(uint32_t)> on_pick) {
    Screen s;
    s.title = title;
    for (size_t i = 0; i < options.size(); i++) {
        const uint32_t v = options[i].value;
        if (v == current) s.selected = static_cast<int>(i);
        s.items.push_back({(v == current ? "* " : "  ") + options[i].label, [this, on_pick, v] {
            pop();
            on_pick(v);
        }});
    }
    return s;
}

// ── Rendering ───────────────────────────────────────────────────────────────

void Menu::draw_status_bar(Framebuffer& fb, const StatusSnapshot& st) {
    // Battery icon with a fill level, then "78% 3.9V"
    bool known = st.battery_mv > 0;
    fb.rect(0, 4, 10, 7, true);
    fb.fill_rect(10, 6, 1, 3, true);
    if (known) fb.fill_rect(1, 5, std::min(8, (st.battery_pct * 8 + 50) / 100), 5, true);
    fb.text(13, 4, known ? fmt("%u%% %u.%uV", u(st.battery_pct), u(st.battery_mv / 1000),
                               u(st.battery_mv % 1000 / 100)).c_str()
                         : "--");

    // Free space, right-aligned before the WiFi indicator
    std::string free = mb(st.free_bytes) + "MB";
    fb.text(108 - Framebuffer::text_width(free.c_str()), 4, free.c_str());

    // "AP" when the access point is on; inverted when the tablet is connected
    if (st.wifi_enabled) {
        if (st.client_connected) fb.fill_rect(111, 3, 17, 9, true);
        fb.text(113, 4, "AP", !st.client_connected);
    }
}

void Menu::draw_home(Framebuffer& fb, const StatusSnapshot& st) {
    std::string state = "Ready";
    if (st.state == api::DeviceState::Compressing) {
        state = fmt("Compressing %u/%u", u(st.compress_done), u(st.compress_total));
    }
    fb.text(0, 18, state.c_str());

    recording::Outcome o = dev_.last_outcome();
    std::string line1, line2;
    if (!o.valid) {
        line1 = fmt("%u recordings", u(st.recording_count));
    } else if (o.saved) {
        uint32_t tenths = o.rate_hz ? static_cast<uint32_t>(o.samples * 10ULL / o.rate_hz) : 0;
        line1 = fmt("Last #%u %u.%us", u(o.id), u(tenths / 10), u(tenths % 10)) + " " + rate_label(o.rate_hz);
        line2 = o.overrun ? "! Samples lost" : "Saved";
    } else {
        line1 = fmt("Last #%u", u(o.id));
        line2 = "! Not saved";
    }
    fb.text(0, 31, line1.c_str());
    fb.text(0, 41, line2.c_str());
    fb.text(0, 56, "Press for menu");
}

void Menu::render(Framebuffer& fb) {
    fb.clear();
    StatusSnapshot st = dev_.status();
    draw_status_bar(fb, st);

    const Screen& s = stack_.back();
    if (s.kind == Kind::Home) {
        draw_home(fb, st);
        dirty_ = false;
        return;
    }

    fb.text(0, TITLE_Y, s.title.c_str());
    fb.fill_rect(0, RULE_Y, Framebuffer::WIDTH, 1, true);

    if (s.kind == Kind::List) {
        for (int i = 0; i < VISIBLE_ROWS; i++) {
            int idx = s.top + i;
            if (idx >= static_cast<int>(s.items.size())) break;
            int  y        = FIRST_ROW_Y + i * ROW_PITCH;
            bool selected = idx == s.selected;
            if (selected) fb.fill_rect(0, y - 1, Framebuffer::WIDTH - 3, ROW_PITCH, true);
            fb.text(2, y, s.items[idx].label.c_str(), !selected);
        }
        // Scrollbar when the list doesn't fit
        int n = static_cast<int>(s.items.size());
        if (n > VISIBLE_ROWS) {
            int track = Framebuffer::HEIGHT - FIRST_ROW_Y;
            int thumb = std::max(4, track * VISIBLE_ROWS / n);
            int y     = FIRST_ROW_Y + (track - thumb) * s.top / (n - VISIBLE_ROWS);
            fb.fill_rect(Framebuffer::WIDTH - 2, y, 2, thumb, true);
        }
    } else {
        for (int i = 0; i < VISIBLE_ROWS; i++) {
            int idx = s.top + i;
            if (idx >= static_cast<int>(s.lines.size())) break;
            fb.text(0, FIRST_ROW_Y + i * ROW_PITCH, s.lines[idx].c_str());
        }
    }
    dirty_ = false;
}

}  // namespace ui
