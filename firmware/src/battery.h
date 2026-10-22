#pragma once

#include <cstdint>
#include "driver/i2c_master.h"

// MAX17043 fuel gauge (spec §5)
void     battery_init(i2c_master_bus_handle_t bus);
void     battery_start_task();
void     battery_read_now();  // immediate read, e.g. right before/after a recording

// Puts the MAX17043 into its < 1 µA sleep mode before deep sleep (spec §7.1).
// battery_init() always wakes it again.
void     battery_sleep();
uint16_t battery_mv();   // cell voltage, millivolts
uint8_t  battery_pct();  // state of charge, integer percent
