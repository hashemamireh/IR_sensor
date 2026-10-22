#pragma once

// Debounced push button with short/long press detection (spec §6.4).
// Press fires on release if held < LONG_PRESS_MS; LongPress fires once,
// while still held, at LONG_PRESS_MS.

#include <cstdint>

namespace ui {

enum class ButtonEvent { None, Press, LongPress };

class ButtonDetector {
public:
    static constexpr uint32_t DEBOUNCE_MS   = 30;
    static constexpr uint32_t LONG_PRESS_MS = 1000;

    // Feed the raw level (true = pressed) every few milliseconds.
    ButtonEvent update(bool raw_pressed, uint32_t now_ms);

private:
    bool     stable_      = false;
    bool     last_raw_    = false;
    uint32_t raw_since_   = 0;
    uint32_t pressed_at_  = 0;
    bool     long_fired_  = false;
};

}  // namespace ui
