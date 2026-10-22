#include "ui.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "battery.h"
#include "button.h"
#include "config.h"
#include "device_state.h"
#include "display.h"
#include "encoder.h"
#include "led.h"
#include "led_color.h"
#include "menu.h"
#include "recorder.h"
#include "recordings.h"
#include "settings_store.h"
#include "storage.h"
#include "wifi_ap.h"

static const char* TAG = "ui";

static constexpr TickType_t LOCK_WAIT = pdMS_TO_TICKS(200);
static constexpr uint32_t   STATUS_REFRESH_MS = 1000;

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

    bool compress_all() override { return false; }  // ponytail: compression job (step 6)
    bool deep_sleep() override { return false; }    // ponytail: deep sleep (step 6)

    bool set_wifi_enabled(bool on) override { return wifi_ap_set_enabled(on) == ESP_OK; }
};

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
    uint32_t         last_activity = now_ms();
    uint32_t         last_draw  = 0;
    api::DeviceState last_state = device_state();

    for (;;) {
        const uint32_t now = now_ms();

        // ── Input ──
        int             steps = encoder_take_steps();
        ui::ButtonEvent ev    = button.update(encoder_button_pressed(), now);
        if (steps != 0 || ev != ui::ButtonEvent::None) {
            last_activity = now;
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
        }

        // ── Device state (e.g. a recording started from the app) ──
        menu.tick();
        const api::DeviceState state = device_state();
        if (state != last_state) {
            last_state    = state;
            last_activity = now;
            if (!screen_on) {
                display_set_power(true);
                screen_on = true;
            }
        }

        // ── Draw: on change, plus a status-bar refresh — never while frozen (§6.2) ──
        bool refresh = !menu.frozen() && now - last_draw >= STATUS_REFRESH_MS;
        if (screen_on && (menu.dirty() || refresh || force_draw)) {
            menu.render(fb);
            display_flush(fb);
            last_draw  = now;
            force_draw = false;
        }

        // ── Screen timeout and brightness — idle only, to keep I2C quiet while busy ──
        if (state == api::DeviceState::Idle) {
            settings::Settings s = settings_get();
            if (screen_on && s.screen_timeout_s > 0 && now - last_activity >= s.screen_timeout_s * 1000) {
                display_set_power(false);
                screen_on = false;
            }
            if (s.brightness != brightness) {
                brightness = s.brightness;
                display_set_brightness(brightness);
            }
        }

        // ── LED ──
        bool low_battery = battery_mv() > 0 && battery_pct() <= ui::LOW_BATTERY_PCT;
        led_set(ui::led_color(state, wifi_ap_client_connected(), low_battery, now));

        vTaskDelay(pdMS_TO_TICKS(UI_TICK_MS));
    }
}

void ui_start(i2c_master_bus_handle_t i2c_bus) {
    display_init(i2c_bus);
    xTaskCreate(task_ui, "ui", 6144, nullptr, 2, nullptr);
    ESP_LOGI(TAG, "UI started");
}
