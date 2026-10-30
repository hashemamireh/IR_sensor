#pragma once

// User settings (spec §6.4 Settings menu, §6.5). Persisted in NVS by the
// device; this module holds the defaults, validation and JSON form.

#include <cstdint>

#include "json_writer.h"
#include "recording.h"

namespace settings {

enum class Brightness : uint8_t { Low, Medium, High };

constexpr uint32_t SCREEN_TIMEOUTS_S[]   = {30, 60, 300, 0};    // 0 = never
constexpr uint32_t SLEEP_TIMEOUTS_S[]    = {300, 900, 1800, 0};  // 0 = never (spec §7.1)
constexpr uint32_t LIVE_TIMEOUTS_S[]     = {60, 300, 600, 1800};  // live view ends (spec §3.6)
constexpr uint32_t DEFAULT_DURATIONS_S[] = {5, 10, 30, 60};

struct Settings {
    Brightness brightness         = Brightness::Medium;
    uint32_t   screen_timeout_s   = 60;
    uint32_t   default_rate_hz    = recording::DEFAULT_SAMPLE_RATE_HZ;
    uint32_t   default_duration_s = 30;
    uint32_t   sleep_timeout_s    = 900;  // deep sleep after this long idle
    uint32_t   live_timeout_s     = 300;  // live view ends after this long untouched
};

const char* brightness_str(Brightness b);
bool parse_brightness(const char* s, Brightness* out);

bool is_valid_screen_timeout(uint32_t s);
bool is_valid_default_duration(uint32_t s);
bool is_valid_sleep_timeout(uint32_t s);
bool is_valid_live_timeout(uint32_t s);

// Checks every field — use after loading from NVS, where values may be stale or corrupt.
bool is_valid(const Settings& s);

// Writes the settings as a JSON object (GET /api/settings).
void write_json(JsonWriter& w, const Settings& s);

}  // namespace settings
