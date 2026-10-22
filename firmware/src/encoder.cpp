#include "encoder.h"

#include "driver/gpio.h"
#include "esp_log.h"

#include "pins.h"

static const char* TAG = "encoder";

void encoder_init() {
    // Configure CLK, DT, SW as inputs with pull-up
    // ponytail: raw GPIO config for now; rotation moves to PCNT and the
    // button to espressif/button when the UI is built.
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
            .intr_type    = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&cfg));
    }
    ESP_LOGI(TAG, "Encoder GPIOs configured (CLK=%d, DT=%d, SW=%d)",
             PIN_ENC_CLK, PIN_ENC_DT, PIN_ENC_SW);
}
