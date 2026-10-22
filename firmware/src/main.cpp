/*
 * IR Sensor Device — Main Firmware
 *
 * Brings up each subsystem per spec.md §9 and starts the FreeRTOS tasks.
 * Subsystem code lives in its own module:
 *   device_state   — idle/recording/… state + lock for flash work
 *   storage        — LittleFS mount
 *   settings_store — user settings in NVS
 *   recordings     — recording files: index, list, delete, IDs
 *   calibration    — ADC raw → mV (eFuse curve fitting)
 *   recorder       — ADC capture → PSRAM → IREC file
 *   ui             — OLED menu, encoder, status LED (display, encoder, led)
 *   battery  — MAX17043 fuel gauge
 *   led      — WS2812B status LED
 *   encoder  — rotary encoder + button
 *   wifi_ap  — WiFi SoftAP
 *   api      — HTTP REST API
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/i2c_master.h"

#include "pins.h"
#include "config.h"
#include "api.h"
#include "battery.h"
#include "calibration.h"
#include "device_state.h"
#include "encoder.h"
#include "led.h"
#include "recorder.h"
#include "recordings.h"
#include "settings_store.h"
#include "storage.h"
#include "ui.h"
#include "wifi_ap.h"

static const char* TAG = "irsensor";

// ── NVS ─────────────────────────────────────────────────────────────────────

static void init_nvs() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");
}

// ── I2C Bus (OLED + MAX17043) ───────────────────────────────────────────────

static i2c_master_bus_handle_t init_i2c() {
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = static_cast<gpio_num_t>(PIN_I2C_SDA),
        .scl_io_num = static_cast<gpio_num_t>(PIN_I2C_SCL),
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = { .enable_internal_pullup = true },
    };
    i2c_master_bus_handle_t bus = nullptr;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));
    // Devices attach in their own modules (OLED via esp_lcd, once the UI lands)
    ESP_LOGI(TAG, "I2C bus initialized");
    return bus;
}

// ── app_main ────────────────────────────────────────────────────────────────

extern "C" void app_main() {
    ESP_LOGI(TAG, "=== IR Sensor Device starting ===");

    device_state_init();
    init_nvs();
    storage_init();
    settings_store_init();
    recordings_init();
    calibration_init();
    i2c_master_bus_handle_t i2c_bus = init_i2c();
    battery_init(i2c_bus);
    led_init();
    encoder_init();
    wifi_ap_init();  // before the ADC is used: its password generator borrows the ADC
    recorder_init();
    api_start();

    // Task priorities per spec §9.3 (single-core, so priorities matter);
    // the recorder and UI tasks are created by recorder_init() and ui_start()
    ui_start(i2c_bus);
    battery_start_task();

    ESP_LOGI(TAG, "=== All subsystems initialized ===");
}
