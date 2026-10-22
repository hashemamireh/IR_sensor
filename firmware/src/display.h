#pragma once

// SSD1306 128×64 OLED over I2C via esp_lcd (spec §6.1). If the panel doesn't
// respond at boot, the firmware runs on without it and these calls do nothing.

#include "driver/i2c_master.h"
#include "framebuffer.h"
#include "settings.h"

bool display_init(i2c_master_bus_handle_t bus);
void display_flush(const ui::Framebuffer& fb);
void display_set_power(bool on);
void display_set_brightness(settings::Brightness b);
