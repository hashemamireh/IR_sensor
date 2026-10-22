#include "power.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"

#include "battery.h"
#include "config.h"
#include "device_state.h"
#include "display.h"
#include "encoder.h"
#include "led.h"
#include "pins.h"
#include "wifi_ap.h"

static const char* TAG = "power";

static constexpr gpio_num_t WAKE_PIN = static_cast<gpio_num_t>(PIN_ENC_SW);

[[noreturn]] static void sleep_now() {
    // Button is active low; keep its pull-up powered while asleep (no external pull-up on most KY-040s)
    ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup_io(1ULL << WAKE_PIN, ESP_EXT1_WAKEUP_ANY_LOW));
    ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(WAKE_PIN));
    ESP_ERROR_CHECK(rtc_gpio_pullup_en(WAKE_PIN));
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    esp_deep_sleep_start();
}

void power_check_wake() {
    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT1) return;

    // The pin is still routed to the LP IO domain after wake
    rtc_gpio_deinit(WAKE_PIN);
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << WAKE_PIN,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    // esp_timer starts at boot, so it's ~time since the press woke us
    int released_ms = 0;
    while (esp_timer_get_time() < WAKE_HOLD_US) {
        released_ms = gpio_get_level(WAKE_PIN) ? released_ms + 10 : 0;
        if (released_ms >= 50) {  // debounced release
            ESP_LOGI(TAG, "Button released before %lld ms — back to sleep", WAKE_HOLD_US / 1000);
            sleep_now();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGI(TAG, "Woken by a long press");
}

bool power_deep_sleep() {
    // Held until sleep (never released), so nothing else touches flash meanwhile
    DeviceLock lock(pdMS_TO_TICKS(200));
    if (!lock.held() || device_state() != api::DeviceState::Idle) return false;

    ESP_LOGI(TAG, "Entering deep sleep — hold the button ~2 s to wake");
    display_set_power(false);
    led_set({0, 0, 0});
    wifi_ap_set_enabled(false);
    battery_sleep();

    // A held button would wake us immediately
    for (int i = 0; i < 300 && encoder_button_pressed(); i++) vTaskDelay(pdMS_TO_TICKS(10));
    sleep_now();
}

static void task_sleep_later(void* arg) {
    vTaskDelay(pdMS_TO_TICKS(reinterpret_cast<uintptr_t>(arg)));
    if (!power_deep_sleep()) ESP_LOGW(TAG, "Deep sleep cancelled — device became busy");
    vTaskDelete(nullptr);
}

void power_deep_sleep_later(uint32_t delay_ms) {
    xTaskCreate(task_sleep_later, "sleep", 3072, reinterpret_cast<void*>(static_cast<uintptr_t>(delay_ms)), 4, nullptr);
}
