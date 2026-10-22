#include "irec.h"

#include <cstring>

namespace irec {

static constexpr uint8_t MAGIC[4] = {'I', 'R', 'E', 'C'};

// Field offsets per spec §4.2
static constexpr size_t OFF_VERSION = 0x04;
static constexpr size_t OFF_RATE    = 0x05;
static constexpr size_t OFF_COUNT   = 0x09;
static constexpr size_t OFF_START   = 0x0D;
static constexpr size_t OFF_CRC     = 0x15;
static constexpr size_t OFF_FLAGS   = 0x19;
static constexpr size_t OFF_ATTEN   = 0x1A;

// Explicit little-endian packing — the header fields are unaligned
static void put_le(uint8_t* p, uint64_t v, int bytes) {
    for (int i = 0; i < bytes; i++) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

static uint64_t get_le(const uint8_t* p, int bytes) {
    uint64_t v = 0;
    for (int i = 0; i < bytes; i++) v |= static_cast<uint64_t>(p[i]) << (8 * i);
    return v;
}

void encode_header(const Header& h, uint8_t out[HEADER_SIZE]) {
    memset(out, 0, HEADER_SIZE);
    memcpy(out, MAGIC, sizeof(MAGIC));
    out[OFF_VERSION] = VERSION;
    put_le(out + OFF_RATE,  h.sample_rate_mhz, 4);
    put_le(out + OFF_COUNT, h.sample_count,    4);
    put_le(out + OFF_START, h.start_utc_us,    8);
    put_le(out + OFF_CRC,   h.crc32,           4);
    out[OFF_FLAGS] = h.flags;
    out[OFF_ATTEN] = h.atten_tenths_db;
}

bool decode_header(const uint8_t in[HEADER_SIZE], Header* out) {
    if (memcmp(in, MAGIC, sizeof(MAGIC)) != 0 || in[OFF_VERSION] != VERSION) return false;
    out->sample_rate_mhz = static_cast<uint32_t>(get_le(in + OFF_RATE,  4));
    out->sample_count    = static_cast<uint32_t>(get_le(in + OFF_COUNT, 4));
    out->start_utc_us    = get_le(in + OFF_START, 8);
    out->crc32           = static_cast<uint32_t>(get_le(in + OFF_CRC,   4));
    out->flags           = in[OFF_FLAGS];
    out->atten_tenths_db = in[OFF_ATTEN];
    return true;
}

uint64_t duration_ms(const Header& h) {
    if (h.sample_rate_mhz == 0) return 0;
    // samples / (mHz / 1000) seconds = samples × 1e6 / mHz milliseconds
    return static_cast<uint64_t>(h.sample_count) * 1000000ULL / h.sample_rate_mhz;
}

// Reflected CRC-32 lookup table, built at compile time
struct Crc32Table {
    uint32_t v[256];
    constexpr Crc32Table() : v() {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            v[i] = c;
        }
    }
};
static constexpr Crc32Table CRC_TABLE;

uint32_t crc32_update(uint32_t crc, const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    crc = ~crc;
    while (len--) crc = CRC_TABLE.v[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

}  // namespace irec
