#include "led.h"

#include "esp_log.h"
#include "led_strip.h"

#include "pins.h"

static const char* TAG = "led";

static led_strip_handle_t rgb_led = nullptr;
static ui::Rgb            shown   = {0, 0, 0};

void led_init() {
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

    led_set({0, 32, 0});  // green = ready, until the UI task takes over
    ESP_LOGI(TAG, "RGB LED initialized");
}

void led_set(ui::Rgb c) {
    if (!rgb_led || c == shown) return;
    led_strip_set_pixel(rgb_led, 0, c.r, c.g, c.b);
    led_strip_refresh(rgb_led);
    shown = c;
}
