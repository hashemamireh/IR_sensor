#include "encoder.h"

#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"

#include "config.h"
#include "pins.h"

static const char* TAG = "encoder";

static constexpr int PCNT_LIMIT = 1000;

static pcnt_unit_handle_t unit = nullptr;
static int consumed = 0;  // counts already turned into steps

void encoder_init() {
    // Button: active low with pull-up
    gpio_config_t sw = {
        .pin_bit_mask = 1ULL << PIN_ENC_SW,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&sw));

    // Rotation: x4 quadrature decoding on two PCNT channels (pull-ups enabled by the driver)
    pcnt_unit_config_t unit_cfg = {};
    unit_cfg.low_limit         = -PCNT_LIMIT;
    unit_cfg.high_limit        = PCNT_LIMIT;
    unit_cfg.flags.accum_count = 1;  // keep counting across the limits

    pcnt_chan_config_t a_cfg = {};
    a_cfg.edge_gpio_num  = PIN_ENC_CLK;
    a_cfg.level_gpio_num = PIN_ENC_DT;
    pcnt_chan_config_t b_cfg = {};
    b_cfg.edge_gpio_num  = PIN_ENC_DT;
    b_cfg.level_gpio_num = PIN_ENC_CLK;
    pcnt_glitch_filter_config_t filter = {.max_glitch_ns = 1000};

    pcnt_channel_handle_t a = nullptr, b = nullptr;
    esp_err_t err = pcnt_new_unit(&unit_cfg, &unit);
    if (err == ESP_OK) err = pcnt_unit_set_glitch_filter(unit, &filter);
    if (err == ESP_OK) err = pcnt_new_channel(unit, &a_cfg, &a);
    if (err == ESP_OK) err = pcnt_new_channel(unit, &b_cfg, &b);
    if (err == ESP_OK) err = pcnt_channel_set_edge_action(a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    if (err == ESP_OK) err = pcnt_channel_set_level_action(a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    if (err == ESP_OK) err = pcnt_channel_set_edge_action(b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    if (err == ESP_OK) err = pcnt_channel_set_level_action(b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    // accum_count needs watch points at the limits
    if (err == ESP_OK) err = pcnt_unit_add_watch_point(unit, PCNT_LIMIT);
    if (err == ESP_OK) err = pcnt_unit_add_watch_point(unit, -PCNT_LIMIT);
    if (err == ESP_OK) err = pcnt_unit_enable(unit);
    if (err == ESP_OK) err = pcnt_unit_clear_count(unit);
    if (err == ESP_OK) err = pcnt_unit_start(unit);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PCNT setup failed: %s — rotation disabled", esp_err_to_name(err));
        unit = nullptr;
        return;
    }
    ESP_LOGI(TAG, "Encoder ready (CLK=%d, DT=%d, SW=%d)", PIN_ENC_CLK, PIN_ENC_DT, PIN_ENC_SW);
}

int encoder_take_steps() {
    if (!unit) return 0;
    int count = 0;
    pcnt_unit_get_count(unit, &count);
    int steps = (count - consumed) / ENCODER_COUNTS_PER_DETENT;
    consumed += steps * ENCODER_COUNTS_PER_DETENT;  // keep the partial detent
    return steps * ENCODER_DIRECTION;
}

bool encoder_button_pressed() {
    return gpio_get_level(static_cast<gpio_num_t>(PIN_ENC_SW)) == 0;
}
