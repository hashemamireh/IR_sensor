#pragma once

// Rotary encoder: rotation decoded in hardware by PCNT, push button read raw
// (debounced and timed by ui::ButtonDetector).

void encoder_init();

// Detents turned since the last call (+ = clockwise, see ENCODER_DIRECTION).
int encoder_take_steps();

// Raw button level — true while pressed.
bool encoder_button_pressed();
