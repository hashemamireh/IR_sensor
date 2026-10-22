#include "settings.h"

#include <cstring>
#include <initializer_list>

namespace settings {

const char* brightness_str(Brightness b) {
    switch (b) {
        case Brightness::Low:    return "low";
        case Brightness::Medium: return "medium";
        case Brightness::High:   return "high";
    }
    return "medium";
}

bool parse_brightness(const char* s, Brightness* out) {
    for (Brightness b : {Brightness::Low, Brightness::Medium, Brightness::High}) {
        if (strcmp(s, brightness_str(b)) == 0) {
            *out = b;
            return true;
        }
    }
    return false;
}

bool is_valid_screen_timeout(uint32_t s) {
    for (uint32_t t : SCREEN_TIMEOUTS_S) {
        if (t == s) return true;
    }
    return false;
}

bool is_valid_default_duration(uint32_t s) {
    for (uint32_t d : DEFAULT_DURATIONS_S) {
        if (d == s) return true;
    }
    return false;
}

bool is_valid(const Settings& s) {
    return s.brightness <= Brightness::High
        && is_valid_screen_timeout(s.screen_timeout_s)
        && recording::is_valid_rate(s.default_rate_hz)
        && is_valid_default_duration(s.default_duration_s);
}

void write_json(JsonWriter& w, const Settings& s) {
    w.begin_object()
        .key("brightness").str(brightness_str(s.brightness))
        .key("screen_timeout_s").num(s.screen_timeout_s)
        .key("default_sample_rate_hz").num(s.default_rate_hz)
        .key("default_duration_s").num(s.default_duration_s)
     .end_object();
}

}  // namespace settings
