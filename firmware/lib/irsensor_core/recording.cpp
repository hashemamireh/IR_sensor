#include "recording.h"

#include <cstdio>
#include <cstring>

#include "irec.h"

namespace recording {

// §3.5: actual rate per nominal rate. Nominal until measured on hardware.
struct ActualRate {
    uint32_t nominal_hz;
    uint32_t actual_mhz;
};
static constexpr ActualRate ACTUAL_RATES[] = {
    {1000,  1000000},
    {5000,  5000000},
    {10000, 10000000},
    {20000, 20000000},
};

uint32_t actual_rate_mhz(uint32_t rate_hz) {
    for (const auto& r : ACTUAL_RATES) {
        if (r.nominal_hz == rate_hz) return r.actual_mhz;
    }
    return 0;
}

uint64_t required_bytes(uint32_t rate_hz, uint32_t duration_s) {
    return irec::HEADER_SIZE + static_cast<uint64_t>(rate_hz) * duration_s * BYTES_PER_SAMPLE;
}

Error validate(uint32_t rate_hz, uint32_t duration_s, uint64_t free_bytes) {
    if (!is_valid_rate(rate_hz)) return Error::InvalidRate;
    if (duration_s == 0 || duration_s > max_duration_s(rate_hz)) return Error::InvalidDuration;
    if (required_bytes(rate_hz, duration_s) + FS_MARGIN_BYTES > free_bytes) {
        return Error::InsufficientSpace;
    }
    return Error::None;
}

const char* error_str(Error e) {
    switch (e) {
        case Error::None:              return "none";
        case Error::InvalidRate:       return "invalid_rate";
        case Error::InvalidDuration:   return "invalid_duration";
        case Error::InsufficientSpace: return "insufficient_space";
    }
    return "unknown";
}

static bool format_path(uint32_t id, const char* ext, char* buf, size_t len) {
    if (id > MAX_ID) return false;
    int n = snprintf(buf, len, "%s/%05u.%s", DIR, static_cast<unsigned>(id), ext);
    return n > 0 && static_cast<size_t>(n) < len;
}

bool path_for_id(uint32_t id, char* buf, size_t len)      { return format_path(id, "irec", buf, len); }
bool temp_path_for_id(uint32_t id, char* buf, size_t len) { return format_path(id, "tmp", buf, len); }

// 1–5 decimal digits, value ≤ MAX_ID
static bool parse_digits(const char* s, size_t n, uint32_t* id) {
    if (n == 0 || n > 5) return false;
    uint32_t v = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + static_cast<uint32_t>(s[i] - '0');
    }
    *id = v;
    return true;
}

bool parse_filename(const char* name, uint32_t* id) {
    return strlen(name) == 10 && strcmp(name + 5, ".irec") == 0 && parse_digits(name, 5, id);
}

bool parse_id(const char* s, uint32_t* id) {
    return parse_digits(s, strlen(s), id);
}

}  // namespace recording
