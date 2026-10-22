#pragma once

// IREC recording file format (spec §4.2, §4.5).
// Hardware-independent — built for the device and for host-side tests.

#include <cstddef>
#include <cstdint>

namespace irec {

constexpr size_t  HEADER_SIZE     = 32;
constexpr uint8_t VERSION         = 0x01;
constexpr uint8_t FLAG_COMPRESSED = 0x01;
constexpr uint8_t FLAG_OVERRUN    = 0x02;  // samples were lost — timing unreliable

struct Header {
    uint32_t sample_rate_mhz = 0;  // actual rate, millihertz (§3.5)
    uint32_t sample_count    = 0;
    uint64_t start_utc_us    = 0;
    uint32_t crc32           = 0;  // CRC-32 of the uncompressed samples
    uint8_t  flags           = 0;
    uint8_t  atten_tenths_db = 0;  // ADC attenuation used, in 0.1 dB (12 dB → 120)
};

// Writes magic, version, fields and the zeroed reserved bytes.
void encode_header(const Header& h, uint8_t out[HEADER_SIZE]);

// False on bad magic or unsupported version.
bool decode_header(const uint8_t in[HEADER_SIZE], Header* out);

// Recording length in milliseconds (0 if the rate is 0).
uint64_t duration_ms(const Header& h);

// Standard CRC-32 (IEEE 802.3 — identical to zlib's crc32()). Start with
// crc = 0 and feed chunks in order: crc = crc32_update(crc, chunk, len).
uint32_t crc32_update(uint32_t crc, const void* data, size_t len);

}  // namespace irec
