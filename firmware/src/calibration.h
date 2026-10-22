#pragma once

// Raw ADC count → millivolts for the OPT101 channel, using the chip's eFuse
// curve-fitting calibration (spec §3.4).

void calibration_init();
bool calibration_available();

// False if calibration is unavailable or the conversion fails.
bool calibration_raw_to_mv(int raw, int* mv);
