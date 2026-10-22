#include "button.h"

namespace ui {

ButtonEvent ButtonDetector::update(bool raw, uint32_t now) {
    if (raw != last_raw_) {
        last_raw_  = raw;
        raw_since_ = now;
    }

    if (raw != stable_ && now - raw_since_ >= DEBOUNCE_MS) {
        stable_ = raw;
        if (stable_) {
            pressed_at_ = now;
            long_fired_ = false;
        } else if (!long_fired_) {
            return ButtonEvent::Press;
        }
    }

    if (stable_ && !long_fired_ && now - pressed_at_ >= LONG_PRESS_MS) {
        long_fired_ = true;
        return ButtonEvent::LongPress;
    }
    return ButtonEvent::None;
}

}  // namespace ui
