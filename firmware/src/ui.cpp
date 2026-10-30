#include "ui.h"

#include <algorithm>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "battery.h"
#include "button.h"
#include "calibration.h"
#include "compressor.h"
#include "config.h"
#include "device_state.h"
#include "display.h"
#include "encoder.h"
#include "led.h"
#include "led_color.h"
#include "menu.h"
#include "power.h"
#include "recorder.h"
#include "recordings.h"
#include "settings_store.h"
#include "storage.h"
#include "wifi_ap.h"

static const char* TAG = "ui";

static constexpr TickType_t LOCK_WAIT = pdMS_TO_TICKS(200);
static constexpr uint32_t   STATUS_REFRESH_MS = 1000;
static constexpr uint32_t   LIVE_REFRESH_MS   = 100;  // live view redraws 10×/s (spec §3.6)

// Raw ADC count → mV with the chip's calibration (identity if there is none)
static uint16_t to_mv(uint16_t raw) {
    int mv = raw;
    calibration_raw_to_mv(raw, &mv);
    return static_cast<uint16_t>(mv);
}

// The menu's view of the firmware. Flash work takes the device lock and is
// refused unless idle, same as the HTTP API.
class FirmwareDevice : public ui::Device {
public:
    ui::StatusSnapshot status() override {
        ui::StatusSnapshot s;
        size_t total = 0, used = 0;
        storage_info(&total, &used);
        s.state            = device_state();
        s.battery_mv       = battery_mv();
        s.battery_pct      = battery_pct();
        s.free_bytes       = total - used;
        s.total_bytes      = total;
        s.recording_count  = recordings_count();
        s.wifi_enabled     = wifi_ap_enabled();
        s.client_connected = wifi_ap_client_connected();
        RecorderProgress p = recorder_progress();
        s.recording_id     = p.id;
        s.elapsed_s        = p.elapsed_s;
        s.total_s          = p.total_s;
        s.rate_hz          = p.rate_hz;
        compressor_progress(&s.compress_done, &s.compress_total);
        return s;
    }

    recording::Outcome last_outcome() override { return recorder_last_outcome(); }
    const char* ssid() override { return wifi_ap_ssid(); }
    const char* password() override { return wifi_ap_password(); }

    std::vector<ui::RecordingSummary> recordings() override {
        std::vector<ui::RecordingSummary> out;
        DeviceLock lock(LOCK_WAIT);
        if (!lock.held() || device_state() != api::DeviceState::Idle) return out;
        recordings_for_each([&](uint32_t id, const irec::Header& h, uint64_t size) {
            out.push_back({id, h, size});
        });
        return out;
    }

    settings::Settings settings() override { return settings_get(); }

    bool save_settings(const settings::Settings& s) override {
        DeviceLock lock(LOCK_WAIT);
        if (!lock.held() || device_state() != api::DeviceState::Idle) return false;
        return settings_save(s) == ESP_OK;
    }

    recording::StartResult start_recording(uint32_t rate_hz, uint32_t duration_s) override {
        uint32_t id;
        return recorder_start(rate_hz, duration_s, &id);
    }

    void stop_recording() override {
        uint32_t id;
        recorder_stop(&id);
    }

    bool delete_recording(uint32_t id) override {
        DeviceLock lock(LOCK_WAIT);
        if (!lock.held() || device_state() != api::DeviceState::Idle) return false;
        return recordings_delete(id) == ESP_OK;
    }

    int delete_all() override {
        DeviceLock lock(LOCK_WAIT);
        if (!lock.held() || device_state() != api::DeviceState::Idle) return -1;
        return static_cast<int>(recordings_delete_all());
    }

    int  compress_all() override { return compressor_start(); }
    bool deep_sleep() override { return power_deep_sleep(); }

    bool set_wifi_enabled(bool on) override { return wifi_ap_set_enabled(on) == ESP_OK; }

    bool start_live() override { return live_start(); }
    void stop_live() override { live_stop(); }

    live::View live_view() override {
        static live::Bucket buckets[live::OLED_COLUMNS * live::BUCKETS_PER_COLUMN];  // UI task only
        size_t     n = live_latest(buckets, sizeof(buckets) / sizeof(buckets[0]));
        live::View v;
        live::make_view(buckets, n, to_mv, &v);
        v.millivolts = calibration_available();
        return v;
    }
};

// Signed difference of two millisecond timestamps — robust to the other one
// being a few ms newer (it's written by other tasks) and to wrap-around
static int32_t ms_since(uint32_t now, uint32_t then) { return static_cast<int32_t>(now - then); }

static uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

static void task_ui(void*) {
    static FirmwareDevice   dev;
    static ui::Menu         menu(dev);
    static ui::Framebuffer  fb;
    ui::ButtonDetector      button;

    if (wifi_ap_password_is_new()) menu.show_wifi_info();  // first boot (spec §8.1)

    settings::Brightness brightness = settings_get().brightness;
    display_set_brightness(brightness);

    bool             screen_on  = true;
    bool             force_draw = true;
    uint32_t         last_input = now_ms();  // encoder, button, state changes — for the screen timeout
    uint32_t         last_busy  = now_ms();  // last moment the device wasn't idle
    uint32_t         last_draw  = 0;
    api::DeviceState last_state = device_state();
    power_note_activity();                   // start the timeouts' clock at boot

    // Which timeout's countdown is on screen, so each only dismisses its own
    enum class Countdown { None, Sleep, Live };
    Countdown countdown = Countdown::None;
    uint32_t  countdown_secs = 0;

    auto show_countdown = [&](Countdown kind, uint32_t left_ms) {
        if (!screen_on) {
            display_set_power(true);
            screen_on = true;
        }
        uint32_t secs = (left_ms + 999) / 1000;
        if (countdown != kind || secs != countdown_secs || !menu.countdown_shown()) {
            if (kind == Countdown::Sleep) menu.show_countdown("Going to sleep", "Press to stay awake", secs);
            else                          menu.show_countdown("Live view ends", "Press to continue", secs);
        }
        countdown      = kind;
        countdown_secs = secs;
    };
    auto clear_countdown = [&](Countdown kind) {
        if (countdown != kind) return;
        menu.dismiss_countdown();
        countdown = Countdown::None;
    };

    for (;;) {
        const uint32_t now = now_ms();

        // ── Input ──
        int             steps = encoder_take_steps();
        ui::ButtonEvent ev    = button.update(encoder_button_pressed(), now);
        if (steps != 0 || ev != ui::ButtonEvent::None) {
            last_input = now;
            power_note_activity();
            if (!screen_on) {
                // Any interaction wakes the screen and is otherwise ignored (spec §7.2)
                display_set_power(true);
                screen_on  = true;
                force_draw = true;
            } else {
                for (; steps > 0; steps--) menu.handle(ui::Input::Right);
                for (; steps < 0; steps++) menu.handle(ui::Input::Left);
                if (ev == ui::ButtonEvent::Press)     menu.handle(ui::Input::Press);
                if (ev == ui::ButtonEvent::LongPress) menu.handle(ui::Input::LongPress);
            }
            if (!menu.countdown_shown()) countdown = Countdown::None;  // input dismissed it
        }

        // ── Device state (e.g. a recording started from the app) ──
        menu.tick();
        const api::DeviceState state = device_state();
        if (state != last_state) {
            last_state = state;
            last_input = now;
            countdown  = Countdown::None;  // a state change resets the menu
            if (!screen_on) {
                display_set_power(true);
                screen_on = true;
            }
        }
        if (state != api::DeviceState::Idle) last_busy = now;

        const settings::Settings s = settings_get();

        // ── Live view timeout (spec §3.6): untouched for live_timeout_s → back to idle ──
        if (state == api::DeviceState::Live) {
            int32_t untouched = std::max<int32_t>(0, ms_since(now, power_last_activity_ms()));
            int32_t left      = static_cast<int32_t>(s.live_timeout_s * 1000) - untouched;
            if (left <= 0) {
                clear_countdown(Countdown::Live);
                live_stop();
            } else if (left <= static_cast<int32_t>(SLEEP_WARNING_S * 1000)) {
                show_countdown(Countdown::Live, left);
            } else {
                clear_countdown(Countdown::Live);  // the app touched it meanwhile
            }
        }

        // ── Idle deep sleep (spec §7.1): counts from the later of user activity
        //    and the device last being busy (recording, live view, …) ──
        if (state == api::DeviceState::Idle && s.sleep_timeout_s > 0) {
            int32_t since_user = ms_since(now, power_last_activity_ms());
            int32_t since_busy = ms_since(now, last_busy);
            int32_t idle       = std::max<int32_t>(0, std::min(since_user, since_busy));
            int32_t left       = static_cast<int32_t>(s.sleep_timeout_s * 1000) - idle;
            if (left <= 0) {
                clear_countdown(Countdown::Sleep);
                if (!power_deep_sleep()) last_busy = now;  // became busy — start over
            } else if (left <= static_cast<int32_t>(SLEEP_WARNING_S * 1000)) {
                show_countdown(Countdown::Sleep, left);
            } else {
                clear_countdown(Countdown::Sleep);  // the app touched it meanwhile
            }
        } else {
            clear_countdown(Countdown::Sleep);
        }

        // ── Draw: on change, plus a periodic refresh — never while frozen (§6.2) ──
        uint32_t refresh_ms = state == api::DeviceState::Live ? LIVE_REFRESH_MS : STATUS_REFRESH_MS;
        bool     refresh    = !menu.frozen() && now - last_draw >= refresh_ms;
        if (screen_on && (menu.dirty() || refresh || force_draw)) {
            menu.render(fb);
            display_flush(fb);
            last_draw  = now;
            force_draw = false;
        }

        // ── Screen timeout (idle only — it stays on in live view) and brightness ──
        if (state == api::DeviceState::Idle && screen_on && !menu.countdown_shown() &&
            s.screen_timeout_s > 0 && ms_since(now, last_input) >= static_cast<int32_t>(s.screen_timeout_s * 1000)) {
            display_set_power(false);
            screen_on = false;
        }
        // I2C — only while idle or in live view, to keep the bus quiet while recording
        if ((state == api::DeviceState::Idle || state == api::DeviceState::Live) && s.brightness != brightness) {
            brightness = s.brightness;
            display_set_brightness(brightness);
        }

        // ── LED — blinks during a countdown ──
        bool    low_battery = battery_mv() > 0 && battery_pct() <= ui::LOW_BATTERY_PCT;
        ui::Rgb color       = ui::led_color(state, wifi_ap_client_connected(), low_battery, now);
        if (menu.countdown_shown() && (now / 250) % 2) color = {0, 0, 0};
        led_set(color);

        vTaskDelay(pdMS_TO_TICKS(UI_TICK_MS));
    }
}

void ui_start(i2c_master_bus_handle_t i2c_bus) {
    display_init(i2c_bus);
    xTaskCreate(task_ui, "ui", 6144, nullptr, 2, nullptr);
    ESP_LOGI(TAG, "UI started");
}
