#pragma once

// Recording parameters, limits and file naming (spec §3.2, §3.5, §4.4, §4.6).
// Hardware-independent — built for the device and for host-side tests.

#include <cstddef>
#include <cstdint>

namespace recording {

constexpr uint32_t SAMPLE_RATES_HZ[]      = {1000, 5000, 10000, 20000};
constexpr uint32_t DEFAULT_SAMPLE_RATE_HZ = 10000;
constexpr uint32_t MAX_DURATION_S         = 300;
constexpr uint32_t MAX_DURATION_20KHZ_S   = 120;  // PSRAM budget (§4.4)
constexpr uint32_t BYTES_PER_SAMPLE       = 2;

// Free space kept in reserve beyond the file itself: LittleFS needs spare
// blocks for metadata and copy-on-write.
constexpr uint64_t FS_MARGIN_BYTES = 64 * 1024;

constexpr const char* DIR    = "/data/recordings";
constexpr uint32_t    MAX_ID = 99999;  // 5-digit filenames

constexpr bool is_valid_rate(uint32_t rate_hz) {
    for (uint32_t r : SAMPLE_RATES_HZ) {
        if (r == rate_hz) return true;
    }
    return false;
}

// Longest allowed recording at this rate; 0 for an invalid rate.
constexpr uint32_t max_duration_s(uint32_t rate_hz) {
    if (!is_valid_rate(rate_hz)) return 0;
    return rate_hz == 20000 ? MAX_DURATION_20KHZ_S : MAX_DURATION_S;
}

// Largest sample buffer any valid rate/duration needs — reserved in PSRAM at boot.
constexpr size_t max_buffer_bytes() {
    size_t largest = 0;
    for (uint32_t r : SAMPLE_RATES_HZ) {
        size_t bytes = static_cast<size_t>(r) * max_duration_s(r) * BYTES_PER_SAMPLE;
        if (bytes > largest) largest = bytes;
    }
    return largest;
}
static_assert(max_buffer_bytes() == 6000000, "PSRAM budget changed — update spec §4.4");

// Actual sample rate in millihertz for a nominal rate (§3.5); 0 for an invalid rate.
uint32_t actual_rate_mhz(uint32_t rate_hz);

// File size of an uncompressed recording: header + raw samples.
uint64_t required_bytes(uint32_t rate_hz, uint32_t duration_s);

enum class Error { None, InvalidRate, InvalidDuration, InsufficientSpace };

// Pre-recording check (§4.4): rate, duration cap and free space.
Error validate(uint32_t rate_hz, uint32_t duration_s, uint64_t free_bytes);

// Short machine-readable name for API error responses, e.g. "invalid_rate".
const char* error_str(Error e);

// "/data/recordings/00042.irec". False if id > MAX_ID or buf is too small.
bool path_for_id(uint32_t id, char* buf, size_t len);

// "/data/recordings/00042.tmp" — written during save, renamed when complete.
bool temp_path_for_id(uint32_t id, char* buf, size_t len);

// Parses a directory entry name like "00042.irec".
bool parse_filename(const char* name, uint32_t* id);

// Parses a URL path segment like "42" or "00042".
bool parse_id(const char* s, uint32_t* id);

}  // namespace recording
