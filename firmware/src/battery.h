#pragma once

#include <cstdint>
#include "driver/i2c_master.h"

// MAX17043 fuel gauge (spec §5)
void     battery_init(i2c_master_bus_handle_t bus);
void     battery_start_task();
uint16_t battery_mv();   // cell voltage, millivolts
uint8_t  battery_pct();  // state of charge, integer percent
