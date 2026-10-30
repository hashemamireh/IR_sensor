#pragma once

// OLED screens and menu (spec §6.1, §6.2, §6.4) as a hardware-independent
// state machine: inputs go in, actions go out through Device, and render()
// draws the current screen into a Framebuffer.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "api_model.h"
#include "framebuffer.h"
#include "irec.h"
#include "recording.h"
#include "settings.h"

namespace ui {

enum class Input { Left, Right, Press, LongPress };

struct StatusSnapshot {
    api::DeviceState state = api::DeviceState::Idle;
    uint16_t battery_mv      = 0;  // 0 = no reading yet
    uint8_t  battery_pct     = 0;
    uint64_t free_bytes      = 0;
    uint64_t total_bytes     = 0;
    uint32_t recording_count = 0;
    bool     wifi_enabled     = false;
    bool     client_connected = false;
    // While recording or saving
    uint32_t recording_id = 0;
    uint32_t elapsed_s    = 0;
    uint32_t total_s      = 0;
    uint32_t rate_hz      = 0;
    // While compressing
    uint32_t compress_done  = 0;
    uint32_t compress_total = 0;
};

struct RecordingSummary {
    uint32_t     id;
    irec::Header header;
    uint64_t     size_bytes;
};

// What the menu needs from the firmware. Implemented by src/ui.cpp, faked in tests.
class Device {
public:
    virtual ~Device() = default;
    virtual StatusSnapshot   status() = 0;
    virtual recording::Outcome last_outcome() = 0;
    virtual const char*      ssid() = 0;
    virtual const char*      password() = 0;
    virtual std::vector<RecordingSummary> recordings() = 0;  // empty when busy
    virtual settings::Settings settings() = 0;
    virtual bool             save_settings(const settings::Settings& s) = 0;
    virtual recording::StartResult start_recording(uint32_t rate_hz, uint32_t duration_s) = 0;
    virtual void             stop_recording() = 0;
    virtual bool             delete_recording(uint32_t id) = 0;
    virtual int              delete_all() = 0;    // count deleted, -1 if busy
    virtual int              compress_all() = 0;  // count queued, -1 if busy, -2 if unavailable
    virtual bool             set_wifi_enabled(bool on) = 0;
    virtual bool             deep_sleep() = 0;    // only returns (false) if busy
};

class Menu {
public:
    explicit Menu(Device& dev);

    void handle(Input in);

    // Follows device state changes, e.g. a recording started from the app.
    void tick();

    void render(Framebuffer& fb);

    // SSID, password and IP — also shown on first boot (spec §8.1).
    void show_wifi_info();

    // "Going to sleep in N s" before the idle deep sleep (spec §7.1). Call again
    // to update the countdown; dismiss when there's activity.
    void show_sleep_warning(uint32_t seconds_left);
    void dismiss_sleep_warning();
    bool sleep_warning_shown() const { return sleep_warning_; }

    // Something changed since the last render().
    bool dirty() const { return dirty_; }

    // The recording screen is up: redraw only when dirty (spec §6.2).
    bool frozen() const;

    // Title of the current screen — for tests.
    const std::string& title() const { return stack_.back().title; }

private:
    struct Item {
        std::string           label;
        std::function<void()> action;  // null = display-only row
    };

    struct Screen {
        enum class Kind { Home, List, Text, Recording, Saving };
        Kind                       kind = Kind::List;
        std::string                title;
        std::vector<Item>          items;  // List
        std::vector<std::string>   lines;  // Text, Recording, Saving
        int                        selected = 0;
        int                        top      = 0;
        std::function<void(Screen&)> build;  // (re)fills title/items/lines
    };
    using Kind = Screen::Kind;

    struct Option {
        std::string label;
        uint32_t    value;
    };

    static void clamp_selection(Screen& s);
    void push(Screen s);
    void pop();
    void reset_home();
    void rebuild_top();
    void move(Screen& s, int delta);
    void message(const std::string& title, std::vector<std::string> lines = {});

    Screen home_screen();
    Screen main_menu();
    Screen record_menu();
    Screen recordings_menu();
    Screen recording_menu(const RecordingSummary& r);
    Screen wifi_menu();
    Screen storage_menu();
    Screen settings_menu();
    Screen recording_screen();
    Screen saving_screen();
    Screen confirm(const std::string& question, std::function<void()> on_yes);
    Screen choice(const std::string& title, const std::vector<Option>& options, uint32_t current,
                  std::function<void(uint32_t)> on_pick);

    void start_recording();
    void update_settings(const std::function<void(settings::Settings&)>& change);

    void draw_status_bar(Framebuffer& fb, const StatusSnapshot& st);
    void draw_home(Framebuffer& fb, const StatusSnapshot& st);

    Device&             dev_;
    std::vector<Screen> stack_;
    api::DeviceState    last_state_;
    uint32_t            rec_rate_hz_;     // "last used" for Quick Start
    uint32_t            rec_duration_s_;
    bool                dirty_ = true;
    bool                sleep_warning_ = false;  // the warning screen is on top
};

}  // namespace ui
