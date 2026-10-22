#pragma once

// Deterministic fake OPT101 signal for exercising the recording pipeline
// without the sensor (the irsensor_synthetic build): a noisy dark baseline
// with a 5 ms IR pulse every 250 ms.

#include <cstdint>

namespace synthetic {

constexpr uint16_t BASELINE    = 200;
constexpr uint16_t PULSE_LEVEL = 2500;
constexpr uint32_t PERIOD_US   = 250000;
constexpr uint32_t PULSE_US    = 5000;
constexpr int      NOISE       = 8;  // ± counts

// Raw ADC value of sample `index` at `rate_hz`.
uint16_t sample(uint64_t index, uint32_t rate_hz);

}  // namespace synthetic
