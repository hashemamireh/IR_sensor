/*
 * IR Sensor Device — Main Firmware
 *
 * Initializes all subsystems per overview.md §9:
 *   - NVS (settings persistence)
 *   - LittleFS (recording storage)
 *   - I2C bus (OLED + MAX17043)
 *   - ADC continuous mode (OPT101, started on-demand)
 *   - WiFi SoftAP + HTTP server
 *   - WS2812B RGB LED
 *   - Rotary encoder input
 *   - FreeRTOS tasks per §9.3
 *
 * This is the compilable scaffold — each init function logs what it would do,
 * so you can build & iterate without hardware connected.
 */

#include <cstdio>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_adc/adc_continuous.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "esp_littlefs.h"
#include "led_strip.h"

#include "pins.h"
#include "config.h"

static const char* TAG = "irsensor";

// ── Forward declarations ────────────────────────────────────────────────────

static void init_nvs();
static void init_littlefs();
static void init_i2c();
static void init_wifi_ap();
static void init_rgb_led();
static void init_encoder();

static void task_ui(void* arg);
static void task_battery_monitor(void* arg);

// ── Global handles ──────────────────────────────────────────────────────────

static i2c_master_bus_handle_t  i2c_bus     = nullptr;
static i2c_master_dev_handle_t  oled_dev    = nullptr;
static i2c_master_dev_handle_t  fuel_dev    = nullptr;
static led_strip_handle_t       rgb_led     = nullptr;
static httpd_handle_t           http_server = nullptr;
static adc_continuous_handle_t  adc_handle  = nullptr;

// ── Shared device state (written by tasks, read by HTTP handler) ────────────

static uint16_t battery_mv  = 0;  // millivolts from VCELL
static uint8_t  battery_pct = 0;  // integer percent from SOC

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

// ── LittleFS ────────────────────────────────────────────────────────────────

static void init_littlefs() {
    esp_vfs_littlefs_conf_t conf = {
        .base_path       = "/data",
        .partition_label = LITTLEFS_PARTITION_LABEL,
        .format_if_mount_failed = true,
        .dont_mount      = false,
    };
    ESP_ERROR_CHECK(esp_vfs_littlefs_register(&conf));

    size_t total = 0, used = 0;
    esp_littlefs_info(LITTLEFS_PARTITION_LABEL, &total, &used);
    ESP_LOGI(TAG, "LittleFS mounted: %zu KB used / %zu KB total", used / 1024, total / 1024);
}

// ── I2C Bus (OLED + MAX17043) ───────────────────────────────────────────────

static void init_i2c() {
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = static_cast<gpio_num_t>(PIN_I2C_SDA),
        .scl_io_num = static_cast<gpio_num_t>(PIN_I2C_SCL),
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = { .enable_internal_pullup = true },
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &i2c_bus));

    // OLED device (SSD1306 @ 0x3C)
    i2c_device_config_t oled_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = I2C_ADDR_OLED,
        .scl_speed_hz    = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &oled_cfg, &oled_dev));

    // MAX17043 fuel gauge @ 0x36
    i2c_device_config_t fuel_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = I2C_ADDR_MAX17043,
        .scl_speed_hz    = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &fuel_cfg, &fuel_dev));

    ESP_LOGI(TAG, "I2C bus initialized — OLED (0x%02X), MAX17043 (0x%02X)",
             I2C_ADDR_OLED, I2C_ADDR_MAX17043);
}

// ── RGB LED (WS2812B via RMT) ───────────────────────────────────────────────

static void init_rgb_led() {
    led_strip_config_t strip_cfg = {
        .strip_gpio_num   = PIN_RGB_LED,
        .max_leds         = 1,
        .led_model        = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = { .invert_out = false },
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src    = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10000000,  // 10 MHz
        .flags = { .with_dma = false },
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &rgb_led));

    // Green = idle/ready
    led_strip_set_pixel(rgb_led, 0, 0, 32, 0);
    led_strip_refresh(rgb_led);
    ESP_LOGI(TAG, "RGB LED initialized (green = ready)");
}

// ── Rotary Encoder ──────────────────────────────────────────────────────────

static void init_encoder() {
    // Configure CLK, DT, SW as inputs with pull-up
    // ponytail: using raw GPIO config here; a proper encoder driver (interrupt +
    // state machine) comes next. This just proves the pins are set up.
    const gpio_num_t enc_pins[] = {
        static_cast<gpio_num_t>(PIN_ENC_CLK),
        static_cast<gpio_num_t>(PIN_ENC_DT),
        static_cast<gpio_num_t>(PIN_ENC_SW),
    };
    for (auto pin : enc_pins) {
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << pin,
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,  // interrupts added later
        };
        ESP_ERROR_CHECK(gpio_config(&cfg));
    }
    ESP_LOGI(TAG, "Encoder GPIOs configured (CLK=%d, DT=%d, SW=%d)",
             PIN_ENC_CLK, PIN_ENC_DT, PIN_ENC_SW);
}

// ── WiFi SoftAP ─────────────────────────────────────────────────────────────

static void init_wifi_ap() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init));

    // Build SSID from last 4 hex chars of MAC
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "%s%02X%02X", WIFI_AP_PREFIX, mac[4], mac[5]);

    wifi_config_t ap_cfg = {};
    strlcpy(reinterpret_cast<char*>(ap_cfg.ap.ssid), ssid, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len       = strlen(ssid);
    ap_cfg.ap.channel        = 1;
    ap_cfg.ap.authmode       = WIFI_AUTH_WPA2_PSK;
    strlcpy(reinterpret_cast<char*>(ap_cfg.ap.password), "irsensor1234", sizeof(ap_cfg.ap.password));
    ap_cfg.ap.max_connection = WIFI_MAX_CLIENTS;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi SoftAP started — SSID: %s", ssid);
}

// ── HTTP Server (REST API stubs) ────────────────────────────────────────────

static esp_err_t handle_status(httpd_req_t* req) {
    size_t total = 0, used = 0;
    esp_littlefs_info(LITTLEFS_PARTITION_LABEL, &total, &used);

    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"state\":\"idle\",\"battery_pct\":%u,\"battery_mv\":%u,\"free_kb\":%zu}",
             battery_pct, battery_mv, (total - used) / 1024);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
}

static void start_http_server() {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&http_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    // GET /api/status
    httpd_uri_t status_uri = {
        .uri      = "/api/status",
        .method   = HTTP_GET,
        .handler  = handle_status,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(http_server, &status_uri);

    // ponytail: remaining endpoints (record, recordings, time, etc.) are
    // registered here as they're implemented. One handler per endpoint,
    // no router abstraction needed — esp_http_server already is one.

    ESP_LOGI(TAG, "HTTP server started — GET /api/status registered");
}

// ── FreeRTOS Tasks ──────────────────────────────────────────────────────────

static void task_ui(void* arg) {
    // ponytail: placeholder loop — OLED draw + encoder read goes here
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void task_battery_monitor(void* arg) {
    const uint8_t reg_vcell = 0x02;
    const uint8_t reg_soc   = 0x04;
    uint8_t buf[2];

    for (;;) {
        // MAX17043: VCELL register (0x02) — upper 12 bits × 1.25 mV
        if (i2c_master_transmit_receive(fuel_dev, &reg_vcell, 1, buf, 2, 100) == ESP_OK) {
            battery_mv = (((uint16_t)buf[0] << 4) | (buf[1] >> 4)) * 5 / 4;
        }
        // MAX17043: SOC register (0x04) — upper byte = integer percent
        if (i2c_master_transmit_receive(fuel_dev, &reg_soc, 1, buf, 2, 100) == ESP_OK) {
            battery_pct = buf[0];
        }

        ESP_LOGD(TAG, "battery: %u mV, %u%%", battery_mv, battery_pct);
        vTaskDelay(pdMS_TO_TICKS(BATTERY_READ_INTERVAL_MS));
    }
}

// ── app_main ────────────────────────────────────────────────────────────────

extern "C" void app_main() {
    ESP_LOGI(TAG, "=== IR Sensor Device starting ===");

    init_nvs();
    init_littlefs();
    init_i2c();
    init_rgb_led();
    init_encoder();
    init_wifi_ap();
    start_http_server();

    // Task priorities per overview §9.3 (single-core, so priorities matter)
    // ADC DMA drain + storage flush tasks are created on-demand when recording starts

    xTaskCreate(task_ui, "ui", 4096, nullptr, 2, nullptr);
    xTaskCreate(task_battery_monitor, "batt", 2048, nullptr, 1, nullptr);

    ESP_LOGI(TAG, "=== All subsystems initialized ===");
}
