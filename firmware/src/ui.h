#pragma once

// UI task: encoder + button → menu → OLED, plus the status LED and screen
// timeout (spec §6, §7.2).

#include "driver/i2c_master.h"

void ui_start(i2c_master_bus_handle_t i2c_bus);
