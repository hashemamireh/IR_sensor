#pragma once

#include "led_color.h"

// Onboard WS2812B status LED (spec §6.3)
void led_init();
void led_set(ui::Rgb c);  // no-op if unchanged
