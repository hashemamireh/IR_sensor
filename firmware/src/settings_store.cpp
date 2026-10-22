#include "settings_store.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs.h"

#include "config.h"

static const char* TAG = "settings";

// NVS keys (max 15 chars)
static constexpr const char* KEY_BRIGHTNESS = "brightness";
static constexpr const char* KEY_TIMEOUT    = "scr_timeout_s";
static constexpr const char* KEY_RATE       = "def_rate_hz";
static constexpr const char* KEY_DURATION   = "def_duration_s";

static SemaphoreHandle_t  mutex = nullptr;
static settings::Settings current;

void settings_store_init() {
    mutex = xSemaphoreCreateMutex();
    configASSERT(mutex);

    settings::Settings s;  // defaults; NVS getters leave missing keys untouched
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        uint8_t b;
        if (nvs_get_u8(h, KEY_BRIGHTNESS, &b) == ESP_OK) s.brightness = static_cast<settings::Brightness>(b);
        nvs_get_u32(h, KEY_TIMEOUT,  &s.screen_timeout_s);
        nvs_get_u32(h, KEY_RATE,     &s.default_rate_hz);
        nvs_get_u32(h, KEY_DURATION, &s.default_duration_s);
        nvs_close(h);
    }
    if (!settings::is_valid(s)) {
        ESP_LOGW(TAG, "Stored settings invalid — using defaults");
        s = settings::Settings{};
    }
    current = s;
    ESP_LOGI(TAG, "Loaded: brightness=%s timeout=%lus rate=%luHz duration=%lus",
             settings::brightness_str(s.brightness), (unsigned long)s.screen_timeout_s,
             (unsigned long)s.default_rate_hz, (unsigned long)s.default_duration_s);
}

settings::Settings settings_get() {
    xSemaphoreTake(mutex, portMAX_DELAY);
    settings::Settings s = current;
    xSemaphoreGive(mutex);
    return s;
}

esp_err_t settings_save(const settings::Settings& s) {
    if (!settings::is_valid(s)) return ESP_ERR_INVALID_ARG;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, KEY_BRIGHTNESS, static_cast<uint8_t>(s.brightness));
    if (err == ESP_OK) err = nvs_set_u32(h, KEY_TIMEOUT,  s.screen_timeout_s);
    if (err == ESP_OK) err = nvs_set_u32(h, KEY_RATE,     s.default_rate_hz);
    if (err == ESP_OK) err = nvs_set_u32(h, KEY_DURATION, s.default_duration_s);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Saving settings failed: %s", esp_err_to_name(err));
        return err;
    }
    xSemaphoreTake(mutex, portMAX_DELAY);
    current = s;
    xSemaphoreGive(mutex);
    return ESP_OK;
}
