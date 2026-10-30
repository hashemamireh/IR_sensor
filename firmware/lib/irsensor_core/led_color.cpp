#include "led_color.h"

namespace ui {

static constexpr uint8_t LEVEL     = 32;  // steady brightness (of 255)
static constexpr uint8_t PULSE_MIN = 2;

// Triangle wave between PULSE_MIN and LEVEL
static uint8_t pulse(uint32_t now_ms) {
    uint32_t phase = now_ms % PULSE_PERIOD_MS;
    uint32_t half  = PULSE_PERIOD_MS / 2;
    uint32_t up    = phase < half ? phase : PULSE_PERIOD_MS - phase;
    return static_cast<uint8_t>(PULSE_MIN + (LEVEL - PULSE_MIN) * up / half);
}

Rgb led_color(api::DeviceState state, bool client_connected, bool low_battery, uint32_t now_ms) {
    if (state == api::DeviceState::Recording || state == api::DeviceState::Saving) {
        return {pulse(now_ms), 0, 0};
    }
    uint8_t level = state == api::DeviceState::Live ? pulse(now_ms) : LEVEL;
    if (low_battery)      return {level, static_cast<uint8_t>(level / 2), 0};
    if (client_connected) return {0, level, level};
    return {0, level, 0};
}

}  // namespace ui
