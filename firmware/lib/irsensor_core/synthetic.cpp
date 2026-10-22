#include "synthetic.h"

namespace synthetic {

uint16_t sample(uint64_t index, uint32_t rate_hz) {
    if (rate_hz == 0) return BASELINE;
    uint64_t t_us = index * 1000000 / rate_hz;
    int value = (t_us % PERIOD_US) < PULSE_US ? PULSE_LEVEL : BASELINE;

    // Cheap hash of the index → noise in [-NOISE, +NOISE]
    uint32_t x = static_cast<uint32_t>(index) * 2654435761u;
    x ^= x >> 16;
    value += static_cast<int>(x % (2 * NOISE + 1)) - NOISE;
    return static_cast<uint16_t>(value);
}

}  // namespace synthetic
