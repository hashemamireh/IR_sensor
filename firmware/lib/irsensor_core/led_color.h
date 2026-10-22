#pragma once

// Status LED colour scheme (spec §6.3).

#include <cstdint>

#include "api_model.h"

namespace ui {

struct Rgb {
    uint8_t r, g, b;
    bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!=(const Rgb& o) const { return !(*this == o); }
};

constexpr uint8_t  LOW_BATTERY_PCT = 15;
constexpr uint32_t PULSE_PERIOD_MS = 1200;

// Recording/saving → red pulsing; low battery → amber; tablet connected →
// cyan (green + blue); otherwise green.
Rgb led_color(api::DeviceState state, bool client_connected, bool low_battery, uint32_t now_ms);

}  // namespace ui
