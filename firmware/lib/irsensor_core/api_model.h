#pragma once

// Device state, request gating and JSON bodies for the REST API (spec §8.2).

#include <cstdint>

#include "irec.h"
#include "json_writer.h"

namespace api {

enum class DeviceState { Idle, Recording, Saving, Compressing };

const char* state_str(DeviceState s);

enum class Route {
    Status,
    RecordStart,
    RecordStop,
    RecordingsList,
    RecordingGet,
    RecordingDelete,
    RecordingsDeleteAll,
    Time,
    Compress,
    SettingsGet,
    SettingsSet,
    Calibration,
    Sleep,
};

// §8.2 "Requests During Recording": false means respond 409 Conflict.
bool route_allowed(DeviceState s, Route r);

struct StatusInfo {
    DeviceState state       = DeviceState::Idle;
    uint16_t    battery_mv  = 0;
    uint8_t     battery_pct = 0;
    uint64_t    free_bytes  = 0;
    uint32_t    recording_count = 0;
    const char* ssid        = "";
    bool        client_connected = false;
    // state == Recording
    uint32_t    elapsed_s    = 0;
    uint32_t    total_s      = 0;
    uint32_t    recording_id = 0;
    // state == Compressing
    uint32_t    compress_done  = 0;
    uint32_t    compress_total = 0;
};

// GET /api/status body
void write_status(JsonWriter& w, const StatusInfo& s);

// One entry of GET /api/recordings
void write_recording(JsonWriter& w, uint32_t id, const irec::Header& h, uint64_t size_bytes);

// {"error": "...", "state": "..."} body for 409 responses
void write_busy(JsonWriter& w, DeviceState s);

// {"error": "<code>"} body for other error responses; adds "field" when given.
void write_error(JsonWriter& w, const char* code, const char* field = nullptr);

// POST /api/time sanity check: between 2024-01-01 and 2100-01-01 UTC.
bool is_plausible_utc_us(int64_t utc_us);

}  // namespace api
