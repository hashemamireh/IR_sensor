#include "api_model.h"

namespace api {

const char* state_str(DeviceState s) {
    switch (s) {
        case DeviceState::Idle:        return "idle";
        case DeviceState::Recording:   return "recording";
        case DeviceState::Saving:      return "saving";
        case DeviceState::Compressing: return "compressing";
    }
    return "unknown";
}

bool route_allowed(DeviceState s, Route r) {
    switch (r) {
        case Route::Status:
        case Route::SettingsGet:
            return true;  // served from RAM
        case Route::RecordStop:
            return s == DeviceState::Recording;
        default:
            return s == DeviceState::Idle;
    }
}

void write_status(JsonWriter& w, const StatusInfo& s) {
    w.begin_object()
        .key("state").str(state_str(s.state))
        .key("battery").begin_object()
            .key("mv").num(s.battery_mv)
            .key("pct").num(s.battery_pct)
        .end_object()
        .key("free_bytes").num(static_cast<int64_t>(s.free_bytes))
        .key("recording_count").num(s.recording_count)
        .key("wifi").begin_object()
            .key("ssid").str(s.ssid)
            .key("client_connected").boolean(s.client_connected)
        .end_object();

    if (s.state == DeviceState::Recording) {
        w.key("recording").begin_object()
            .key("id").num(s.recording_id)
            .key("elapsed_s").num(s.elapsed_s)
            .key("total_s").num(s.total_s)
        .end_object();
    } else if (s.state == DeviceState::Compressing) {
        w.key("compress").begin_object()
            .key("done").num(s.compress_done)
            .key("total").num(s.compress_total)
        .end_object();
    }
    w.end_object();
}

void write_recording(JsonWriter& w, uint32_t id, const irec::Header& h, uint64_t size_bytes) {
    w.begin_object()
        .key("id").num(id)
        .key("start_utc_us").num(static_cast<int64_t>(h.start_utc_us))
        .key("duration_ms").num(static_cast<int64_t>(irec::duration_ms(h)))
        .key("sample_rate_mhz").num(h.sample_rate_mhz)
        .key("sample_count").num(h.sample_count)
        .key("size_bytes").num(static_cast<int64_t>(size_bytes))
        .key("compressed").boolean(h.flags & irec::FLAG_COMPRESSED)
     .end_object();
}

void write_busy(JsonWriter& w, DeviceState s) {
    w.begin_object()
        .key("error").str("busy")
        .key("state").str(state_str(s))
     .end_object();
}

void write_error(JsonWriter& w, const char* code, const char* field) {
    w.begin_object().key("error").str(code);
    if (field) w.key("field").str(field);
    w.end_object();
}

bool is_plausible_utc_us(int64_t utc_us) {
    constexpr int64_t MIN_UTC_US = 1704067200LL * 1000000;  // 2024-01-01T00:00:00Z
    constexpr int64_t MAX_UTC_US = 4102444800LL * 1000000;  // 2100-01-01T00:00:00Z
    return utc_us >= MIN_UTC_US && utc_us < MAX_UTC_US;
}

}  // namespace api
