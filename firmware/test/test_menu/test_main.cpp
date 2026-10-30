#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unity.h>

#include "menu.h"

using api::DeviceState;
using recording::StartResult;
using ui::Input;
using ui::Menu;

void setUp() {}
void tearDown() {}

struct FakeDevice : ui::Device {
    ui::StatusSnapshot st;
    recording::Outcome outcome;
    settings::Settings cfg;
    std::vector<ui::RecordingSummary> recs;
    StartResult next_start = StartResult::Ok;

    uint32_t started_rate = 0, started_duration = 0;
    int      stops = 0;
    uint32_t deleted_id = 0;
    int      saves = 0;

    FakeDevice() {
        st.battery_mv = 3900;
        st.battery_pct = 78;
        st.free_bytes = 15200000;
        st.total_bytes = 28900000;
        st.wifi_enabled = true;
        for (uint32_t id : {1u, 2u, 3u}) {
            irec::Header h;
            h.sample_rate_mhz = 10000000;
            h.sample_count = 300000;
            h.start_utc_us = 1792592580000000ULL + id * 60000000ULL;  // 2026-10-21 ~14:23 UTC
            recs.push_back({id, h, 600032});
        }
        st.recording_count = recs.size();
    }

    ui::StatusSnapshot status() override { return st; }
    recording::Outcome last_outcome() override { return outcome; }
    const char* ssid() override { return "IRSensor-AB12"; }
    const char* password() override { return "abcd23efgh"; }
    std::vector<ui::RecordingSummary> recordings() override { return recs; }
    settings::Settings settings() override { return cfg; }
    bool save_settings(const settings::Settings& s) override { cfg = s; saves++; return true; }
    StartResult start_recording(uint32_t rate, uint32_t duration) override {
        started_rate = rate;
        started_duration = duration;
        if (next_start == StartResult::Ok) {
            st.state = DeviceState::Recording;
            st.recording_id = 4;
            st.total_s = duration;
            st.rate_hz = rate;
        }
        return next_start;
    }
    void stop_recording() override { stops++; }
    bool delete_recording(uint32_t id) override {
        deleted_id = id;
        for (auto it = recs.begin(); it != recs.end(); ++it) {
            if (it->id == id) { recs.erase(it); return true; }
        }
        return false;
    }
    int  delete_all() override { int n = recs.size(); recs.clear(); return n; }
    int  compress_all() override {
        if (st.state != DeviceState::Idle) return -1;
        st.state = DeviceState::Compressing;
        st.compress_total = recs.size();
        return recs.size();
    }
    bool set_wifi_enabled(bool on) override { st.wifi_enabled = on; return true; }
    bool deep_sleep() override { sleeps++; return false; }  // the real one only returns when busy
    int  sleeps = 0;

    bool live_busy = false;
    int  live_stops = 0;
    live::View view;
    bool start_live() override {
        if (live_busy) return false;
        st.state = DeviceState::Live;
        return true;
    }
    void stop_live() override { live_stops++; }
    live::View live_view() override { return view; }
};

// Writes the rendered screen as ASCII art when UI_PREVIEW_DIR is set
static void preview(Menu& m, const char* name) {
    ui::Framebuffer fb;
    m.render(fb);
    const char* dir = getenv("UI_PREVIEW_DIR");
    if (!dir) return;
    std::string path = std::string(dir) + "/" + name + ".txt";
    if (FILE* f = fopen(path.c_str(), "w")) {
        fputs(fb.to_ascii().c_str(), f);
        fclose(f);
    }
}

static void press(Menu& m, int n = 1) { while (n--) m.handle(Input::Press); }
static void right(Menu& m, int n = 1) { while (n--) m.handle(Input::Right); }

static void test_home_menu_and_back() {
    FakeDevice dev;
    Menu m(dev);
    preview(m, "01_home");
    press(m);
    TEST_ASSERT_EQUAL_STRING("Menu", m.title().c_str());
    preview(m, "02_main_menu");
    m.handle(Input::LongPress);
    TEST_ASSERT_FALSE(m.frozen());
    press(m);
    TEST_ASSERT_EQUAL_STRING("Menu", m.title().c_str());  // was back on home
}

static void test_quick_start_and_stop() {
    FakeDevice dev;
    Menu m(dev);
    press(m);      // main menu
    press(m);      // Record
    TEST_ASSERT_EQUAL_STRING("Record", m.title().c_str());
    preview(m, "03_record_menu");
    press(m);      // Start 30s @ 10kHz
    TEST_ASSERT_EQUAL_UINT32(10000, dev.started_rate);
    TEST_ASSERT_EQUAL_UINT32(30, dev.started_duration);
    TEST_ASSERT_TRUE(m.frozen());
    TEST_ASSERT_EQUAL_STRING("Recording #4", m.title().c_str());
    preview(m, "04_recording");

    press(m);
    TEST_ASSERT_EQUAL_INT(1, dev.stops);

    dev.st.state = DeviceState::Saving;
    m.tick();
    TEST_ASSERT_FALSE(m.frozen());
    TEST_ASSERT_EQUAL_STRING("Saving #4", m.title().c_str());
    preview(m, "05_saving");

    dev.st.state = DeviceState::Idle;
    dev.outcome = {true, 4, true, false, 300000, 10000};
    m.tick();
    press(m);
    TEST_ASSERT_EQUAL_STRING("Menu", m.title().c_str());  // was on home
    m.handle(Input::LongPress);
    preview(m, "06_home_after_recording");
}

static void test_recording_started_from_app_takes_over_screen() {
    FakeDevice dev;
    Menu m(dev);
    press(m, 2);  // inside Record menu
    dev.st.state = DeviceState::Recording;
    dev.st.recording_id = 9;
    m.tick();
    TEST_ASSERT_TRUE(m.frozen());
    TEST_ASSERT_EQUAL_STRING("Recording #9", m.title().c_str());
}

static void test_20khz_caps_duration() {
    FakeDevice dev;
    Menu m(dev);
    press(m, 2);       // Record
    right(m, 1);
    press(m);          // Duration
    right(m, 10);      // last option
    press(m);
    TEST_ASSERT_EQUAL_STRING("Record", m.title().c_str());
    right(m, 1);       // Rate
    press(m);
    right(m, 10);      // 20 kHz
    press(m);
    m.handle(Input::Left);
    m.handle(Input::Left);
    press(m);          // Start
    TEST_ASSERT_EQUAL_UINT32(20000, dev.started_rate);
    TEST_ASSERT_EQUAL_UINT32(120, dev.started_duration);  // 300 s clamped
}

static void test_start_refused_shows_warning() {
    FakeDevice dev;
    dev.next_start = StartResult::InsufficientSpace;
    Menu m(dev);
    press(m, 3);
    TEST_ASSERT_EQUAL_STRING("Not enough space", m.title().c_str());
    preview(m, "07_not_enough_space");
    press(m);
    TEST_ASSERT_EQUAL_STRING("Record", m.title().c_str());
}

static void test_delete_recording() {
    FakeDevice dev;
    Menu m(dev);
    press(m);           // main
    right(m, 2);
    press(m);           // Recordings
    TEST_ASSERT_EQUAL_STRING("Recordings (3)", m.title().c_str());
    preview(m, "08_recordings");
    press(m);           // newest first → #3
    TEST_ASSERT_EQUAL_STRING("Recording #3", m.title().c_str());
    press(m);           // View Info
    preview(m, "09_recording_info");
    press(m);           // dismiss
    right(m);
    press(m);           // Delete
    TEST_ASSERT_EQUAL_STRING("Delete #3?", m.title().c_str());
    press(m);           // "No" is the default
    TEST_ASSERT_EQUAL_STRING("Recording #3", m.title().c_str());
    press(m);           // Delete again
    right(m);
    press(m);           // Yes
    TEST_ASSERT_EQUAL_UINT32(3, dev.deleted_id);
    TEST_ASSERT_EQUAL_STRING("Recordings (2)", m.title().c_str());
}

static void test_settings_change_is_saved() {
    FakeDevice dev;
    Menu m(dev);
    press(m);
    right(m, 5);
    press(m);           // Settings
    preview(m, "10_settings");
    press(m);           // Brightness
    right(m);           // Medium → High
    press(m);
    TEST_ASSERT_EQUAL_INT(1, dev.saves);
    TEST_ASSERT_TRUE(dev.cfg.brightness == settings::Brightness::High);
    TEST_ASSERT_EQUAL_STRING("Settings", m.title().c_str());
}

static void test_wifi_info_and_toggle() {
    FakeDevice dev;
    Menu m(dev);
    m.show_wifi_info();
    TEST_ASSERT_EQUAL_STRING("WiFi", m.title().c_str());
    preview(m, "11_wifi_info");
    press(m);
    press(m);
    right(m, 3);
    press(m);           // WiFi menu
    press(m);           // AP: On → Off
    TEST_ASSERT_FALSE(dev.st.wifi_enabled);
}

static void test_status_bar_shows_connection() {
    FakeDevice dev;
    dev.st.client_connected = true;
    Menu m(dev);
    ui::Framebuffer fb;
    m.render(fb);
    TEST_ASSERT_TRUE(fb.pixel(112, 4));  // inverted "AP" box
    dev.st.client_connected = false;
    m.render(fb);
    TEST_ASSERT_FALSE(fb.pixel(112, 4));
}

static void test_compress_all_shows_progress_on_home() {
    FakeDevice dev;
    Menu m(dev);
    press(m);
    right(m, 4);
    press(m);           // Storage
    TEST_ASSERT_EQUAL_STRING("Storage", m.title().c_str());
    preview(m, "12_storage");
    right(m, 2);
    press(m);           // Compress All
    right(m);
    press(m);           // Yes
    TEST_ASSERT_TRUE(dev.st.state == DeviceState::Compressing);
    dev.st.compress_done = 1;
    m.tick();
    preview(m, "13_compressing");
    press(m);
    TEST_ASSERT_EQUAL_STRING("Menu", m.title().c_str());  // was on home
}

static void test_deep_sleep_refused_shows_message() {
    FakeDevice dev;
    Menu m(dev);
    press(m);
    right(m, 6);
    press(m);           // Deep Sleep
    TEST_ASSERT_EQUAL_STRING("Deep sleep?", m.title().c_str());
    right(m);
    press(m);           // Yes
    TEST_ASSERT_EQUAL_INT(1, dev.sleeps);
    TEST_ASSERT_EQUAL_STRING("Device busy", m.title().c_str());
}

static void test_sleep_timeout_setting() {
    FakeDevice dev;
    Menu m(dev);
    press(m);
    right(m, 5);
    press(m);           // Settings
    right(m, 4);
    press(m);           // Sleep after
    TEST_ASSERT_EQUAL_STRING("Sleep after", m.title().c_str());
    right(m, 10);       // Never
    press(m);
    TEST_ASSERT_EQUAL_UINT32(0, dev.cfg.sleep_timeout_s);
}

static void test_sleep_warning_counts_down_and_any_input_dismisses() {
    FakeDevice dev;
    Menu m(dev);
    press(m);           // in the main menu
    m.show_countdown("Going to sleep", "Press to stay awake", 10);
    TEST_ASSERT_TRUE(m.countdown_shown());
    TEST_ASSERT_EQUAL_STRING("Going to sleep", m.title().c_str());
    preview(m, "14_sleep_warning");
    m.show_countdown("Going to sleep", "Press to stay awake", 9);  // updates in place
    m.handle(Input::Right);    // even a turn means "stay awake"
    TEST_ASSERT_FALSE(m.countdown_shown());
    TEST_ASSERT_EQUAL_STRING("Menu", m.title().c_str());  // back where we were

    m.show_countdown("Going to sleep", "Press to stay awake", 5);
    m.dismiss_countdown();
    TEST_ASSERT_EQUAL_STRING("Menu", m.title().c_str());
}

static void fill_view(live::View& v) {
    v.valid = true;
    v.millivolts = true;
    v.now = 412;
    v.peak = 1850;
    v.columns = live::OLED_COLUMNS;
    for (size_t i = 0; i < v.columns; i++) {
        v.col_min[i] = 380 + (i * 7) % 40;
        v.col_max[i] = v.col_min[i] + 20;
        if (i % 25 == 3) v.col_max[i] = 1850;  // pulses
    }
}

static void test_live_view_press_records_long_press_exits() {
    FakeDevice dev;
    fill_view(dev.view);
    Menu m(dev);
    press(m);
    right(m);
    press(m);           // Live View
    TEST_ASSERT_EQUAL_STRING("Live", m.title().c_str());
    TEST_ASSERT_FALSE(m.frozen());
    preview(m, "15_live_view");

    // The countdown sits on top; any input dismisses it without recording
    m.show_countdown("Live view ends", "Press to continue", 10);
    preview(m, "16_live_ends");
    press(m);
    TEST_ASSERT_EQUAL_STRING("Live", m.title().c_str());
    TEST_ASSERT_EQUAL_UINT32(0, dev.started_rate);

    m.handle(Input::LongPress);
    TEST_ASSERT_EQUAL_INT(1, dev.live_stops);

    press(m);           // records with the Record menu's settings
    TEST_ASSERT_EQUAL_UINT32(10000, dev.started_rate);
    TEST_ASSERT_TRUE(m.frozen());
    TEST_ASSERT_EQUAL_STRING("Recording #4", m.title().c_str());
}

static void test_live_view_ends_back_to_home_and_busy_refusal() {
    FakeDevice dev;
    Menu m(dev);
    press(m);
    right(m);
    press(m);           // Live View
    dev.st.state = DeviceState::Idle;
    m.tick();
    press(m);
    TEST_ASSERT_EQUAL_STRING("Menu", m.title().c_str());  // was on home

    dev.live_busy = true;
    right(m);
    press(m);
    TEST_ASSERT_EQUAL_STRING("Device busy", m.title().c_str());
}

static void test_live_view_without_data_yet() {
    FakeDevice dev;
    dev.st.state = DeviceState::Live;
    Menu m(dev);
    m.tick();
    ui::Framebuffer fb;
    m.render(fb);  // must not crash with an empty view
    TEST_ASSERT_FALSE(m.dirty());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_home_menu_and_back);
    RUN_TEST(test_quick_start_and_stop);
    RUN_TEST(test_recording_started_from_app_takes_over_screen);
    RUN_TEST(test_20khz_caps_duration);
    RUN_TEST(test_start_refused_shows_warning);
    RUN_TEST(test_delete_recording);
    RUN_TEST(test_settings_change_is_saved);
    RUN_TEST(test_wifi_info_and_toggle);
    RUN_TEST(test_status_bar_shows_connection);
    RUN_TEST(test_compress_all_shows_progress_on_home);
    RUN_TEST(test_deep_sleep_refused_shows_message);
    RUN_TEST(test_sleep_timeout_setting);
    RUN_TEST(test_sleep_warning_counts_down_and_any_input_dismisses);
    RUN_TEST(test_live_view_press_records_long_press_exits);
    RUN_TEST(test_live_view_ends_back_to_home_and_busy_refusal);
    RUN_TEST(test_live_view_without_data_yet);
    return UNITY_END();
}
