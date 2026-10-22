#include "api.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sys/time.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"

#include "api_model.h"
#include "battery.h"
#include "calibration.h"
#include "compressor.h"
#include "config.h"
#include "device_state.h"
#include "json_writer.h"
#include "power.h"
#include "recorder.h"
#include "recording.h"
#include "recordings.h"
#include "settings_store.h"
#include "storage.h"
#include "wifi_ap.h"

using api::Route;

static const char* TAG = "api";

static constexpr size_t MAX_BODY_BYTES = 512;
static constexpr size_t DOWNLOAD_CHUNK = 4096;
static constexpr const char* RECORDINGS_PREFIX = "/api/recordings/";

static httpd_handle_t http_server = nullptr;

// ── Response helpers ────────────────────────────────────────────────────────

static esp_err_t send_json(httpd_req_t* req, const char* status, const JsonWriter& w) {
    if (!w.ok()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "response too large");
    }
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, w.c_str(), w.length());
}

static esp_err_t send_error(httpd_req_t* req, const char* status, const char* code,
                            const char* field = nullptr) {
    char buf[128];
    JsonWriter w(buf, sizeof(buf));
    api::write_error(w, code, field);
    return send_json(req, status, w);
}

static esp_err_t send_busy(httpd_req_t* req) {
    char buf[64];
    JsonWriter w(buf, sizeof(buf));
    api::write_busy(w, device_state());
    return send_json(req, "409 Conflict", w);
}

static esp_err_t send_deleted(httpd_req_t* req, uint32_t n) {
    char buf[32];
    JsonWriter w(buf, sizeof(buf));
    w.begin_object().key("deleted").num(n).end_object();
    return send_json(req, "200 OK", w);
}

// ── Request helpers ─────────────────────────────────────────────────────────

// Reads and parses a JSON object body. On failure *out is nullptr and the
// return value is what the handler should return (a 400 was sent, or the
// connection failed).
static esp_err_t read_json_body(httpd_req_t* req, cJSON** out) {
    *out = nullptr;
    if (req->content_len == 0 || req->content_len > MAX_BODY_BYTES) {
        return send_error(req, "400 Bad Request", "invalid_body");
    }

    char buf[MAX_BODY_BYTES + 1];
    size_t got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, buf + got, req->content_len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) return ESP_FAIL;
        got += n;
    }
    buf[got] = '\0';

    cJSON* json = cJSON_Parse(buf);
    if (!cJSON_IsObject(json)) {
        cJSON_Delete(json);
        return send_error(req, "400 Bad Request", "invalid_json");
    }
    *out = json;
    return ESP_OK;
}

// A JSON number that's a whole value in [0, max]
static bool get_uint(const cJSON* item, uint64_t max, uint64_t* out) {
    if (!cJSON_IsNumber(item)) return false;
    double v = item->valuedouble;
    if (v < 0 || v > static_cast<double>(max) || v != std::floor(v)) return false;
    *out = static_cast<uint64_t>(v);
    return true;
}

// Recording ID from "/api/recordings/{id}"
static bool id_from_uri(httpd_req_t* req, uint32_t* id) {
    const char* seg = req->uri + strlen(RECORDINGS_PREFIX);
    char buf[8];
    size_t n = strcspn(seg, "?");
    if (n >= sizeof(buf)) return false;
    memcpy(buf, seg, n);
    buf[n] = '\0';
    return recording::parse_id(buf, id);
}

// ── GET /api/status ─────────────────────────────────────────────────────────

static esp_err_t handle_status(httpd_req_t* req) {
    size_t total = 0, used = 0;
    storage_info(&total, &used);

    api::StatusInfo s;
    s.state            = device_state();
    s.battery_mv       = battery_mv();
    s.battery_pct      = battery_pct();
    s.free_bytes       = total - used;
    s.recording_count  = recordings_count();
    s.ssid             = wifi_ap_ssid();
    s.client_connected = wifi_ap_client_connected();
    if (s.state == api::DeviceState::Recording) {
        RecorderProgress p = recorder_progress();
        s.recording_id = p.id;
        s.elapsed_s    = p.elapsed_s;
        s.total_s      = p.total_s;
    }
    if (s.state == api::DeviceState::Compressing) {
        compressor_progress(&s.compress_done, &s.compress_total);
    }

    char buf[320];
    JsonWriter w(buf, sizeof(buf));
    api::write_status(w, s);
    return send_json(req, "200 OK", w);
}

// ── POST /api/record, /api/record/stop ──────────────────────────────────────

static esp_err_t handle_record_start(httpd_req_t* req) {
    if (!api::route_allowed(device_state(), Route::RecordStart)) return send_busy(req);

    cJSON* body;
    esp_err_t err = read_json_body(req, &body);
    if (!body) return err;

    uint64_t rate = 0, duration = 0;
    bool rate_ok     = get_uint(cJSON_GetObjectItemCaseSensitive(body, "sample_rate_hz"), UINT32_MAX, &rate);
    bool duration_ok = get_uint(cJSON_GetObjectItemCaseSensitive(body, "duration_s"), UINT32_MAX, &duration);
    cJSON_Delete(body);
    if (!rate_ok)     return send_error(req, "400 Bad Request", "invalid_rate");
    if (!duration_ok) return send_error(req, "400 Bad Request", "invalid_duration");

    uint32_t id = 0;
    switch (recorder_start(rate, duration, &id)) {
        case StartResult::Ok:                break;
        case StartResult::Busy:              return send_busy(req);
        case StartResult::InvalidRate:       return send_error(req, "400 Bad Request", "invalid_rate");
        case StartResult::InvalidDuration:   return send_error(req, "400 Bad Request", "invalid_duration");
        case StartResult::InsufficientSpace: return send_error(req, "507 Insufficient Storage", "insufficient_space");
        case StartResult::StorageError:      return send_error(req, "500 Internal Server Error", "storage_error");
    }

    char buf[96];
    JsonWriter w(buf, sizeof(buf));
    w.begin_object()
        .key("id").num(id)
        .key("duration_s").num(static_cast<int64_t>(duration))
        .key("sample_rate_hz").num(static_cast<int64_t>(rate))
     .end_object();
    return send_json(req, "200 OK", w);
}

static esp_err_t handle_record_stop(httpd_req_t* req) {
    uint32_t id = 0;
    if (!recorder_stop(&id)) return send_busy(req);

    char buf[32];
    JsonWriter w(buf, sizeof(buf));
    w.begin_object().key("id").num(id).end_object();
    return send_json(req, "200 OK", w);
}

// ── /api/recordings ─────────────────────────────────────────────────────────

static esp_err_t handle_recordings_list(httpd_req_t* req) {
    DeviceLock lock;
    if (!api::route_allowed(device_state(), Route::RecordingsList)) return send_busy(req);

    // From the in-RAM index, streamed in ~1 KB chunks — the list can hold
    // thousands of recordings (one entry is ~200 bytes)
    static constexpr size_t FLUSH_AT = 1024;
    char buf[FLUSH_AT + 256];
    JsonWriter w(buf, sizeof(buf));
    w.begin_object().key("recordings").begin_array();
    httpd_resp_set_type(req, "application/json");

    esp_err_t err = ESP_OK;
    recordings_for_each([&](uint32_t id, const irec::Header& h, uint64_t size) {
        if (err != ESP_OK) return;
        api::write_recording(w, id, h, size);
        if (!w.ok()) {
            err = ESP_FAIL;
        } else if (w.length() >= FLUSH_AT) {
            err = httpd_resp_send_chunk(req, w.c_str(), w.length());
            w.clear_buffer();
        }
    });
    if (err != ESP_OK) return err;

    w.end_array().end_object();
    err = httpd_resp_send_chunk(req, w.c_str(), w.length());
    if (err == ESP_OK) err = httpd_resp_send_chunk(req, nullptr, 0);
    return err;
}

static esp_err_t handle_recording_get(httpd_req_t* req) {
    DeviceLock lock;
    if (!api::route_allowed(device_state(), Route::RecordingGet)) return send_busy(req);

    uint32_t id;
    uint64_t size = 0;
    FILE* f = id_from_uri(req, &id) ? recordings_open(id, &size) : nullptr;
    if (!f) return send_error(req, "404 Not Found", "not_found");

    char* chunk = static_cast<char*>(malloc(DOWNLOAD_CHUNK));
    if (!chunk) {
        fclose(f);
        return send_error(req, "500 Internal Server Error", "storage_error");
    }

    char disposition[48];
    snprintf(disposition, sizeof(disposition), "attachment; filename=\"%05lu.irec\"",
             static_cast<unsigned long>(id));
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Content-Disposition", disposition);

    // A failed read ends the response without the final chunk, so the
    // client sees a broken transfer rather than a short file
    esp_err_t err = ESP_OK;
    size_t n;
    while (err == ESP_OK && (n = fread(chunk, 1, DOWNLOAD_CHUNK, f)) > 0) {
        err = httpd_resp_send_chunk(req, chunk, n);
    }
    if (err == ESP_OK && ferror(f)) err = ESP_FAIL;
    free(chunk);
    fclose(f);

    if (err == ESP_OK) err = httpd_resp_send_chunk(req, nullptr, 0);
    return err;
}

static esp_err_t handle_recording_delete(httpd_req_t* req) {
    DeviceLock lock;
    if (!api::route_allowed(device_state(), Route::RecordingDelete)) return send_busy(req);

    uint32_t id;
    esp_err_t err = id_from_uri(req, &id) ? recordings_delete(id) : ESP_ERR_NOT_FOUND;
    if (err == ESP_ERR_NOT_FOUND) return send_error(req, "404 Not Found", "not_found");
    if (err != ESP_OK) return send_error(req, "500 Internal Server Error", "storage_error");
    return send_deleted(req, 1);
}

static esp_err_t handle_recordings_delete_all(httpd_req_t* req) {
    DeviceLock lock;
    if (!api::route_allowed(device_state(), Route::RecordingsDeleteAll)) return send_busy(req);

    cJSON* body;
    esp_err_t err = read_json_body(req, &body);
    if (!body) return err;

    // Body must be exactly {"confirm": "DELETE_ALL"} (spec §8.2)
    const cJSON* confirm = cJSON_GetObjectItemCaseSensitive(body, "confirm");
    bool confirmed = cJSON_GetArraySize(body) == 1 && cJSON_IsString(confirm)
                  && strcmp(confirm->valuestring, "DELETE_ALL") == 0;
    cJSON_Delete(body);
    if (!confirmed) return send_error(req, "400 Bad Request", "confirmation_required");

    return send_deleted(req, recordings_delete_all());
}

// ── POST /api/time ──────────────────────────────────────────────────────────

static esp_err_t handle_time(httpd_req_t* req) {
    DeviceLock lock;
    if (!api::route_allowed(device_state(), Route::Time)) return send_busy(req);

    cJSON* body;
    esp_err_t err = read_json_body(req, &body);
    if (!body) return err;

    // µs since epoch fit exactly in a double until 2255
    uint64_t utc_us = 0;
    bool valid = get_uint(cJSON_GetObjectItemCaseSensitive(body, "utc_us"), INT64_MAX, &utc_us)
              && api::is_plausible_utc_us(static_cast<int64_t>(utc_us));
    cJSON_Delete(body);
    if (!valid) return send_error(req, "400 Bad Request", "invalid_time");

    // System clock keeps running through deep sleep (spec §7.3)
    timeval tv = {
        .tv_sec  = static_cast<time_t>(utc_us / 1000000),
        .tv_usec = static_cast<suseconds_t>(utc_us % 1000000),
    };
    settimeofday(&tv, nullptr);

    gettimeofday(&tv, nullptr);
    char buf[48];
    JsonWriter w(buf, sizeof(buf));
    w.begin_object()
        .key("utc_us").num(static_cast<int64_t>(tv.tv_sec) * 1000000 + tv.tv_usec)
     .end_object();
    ESP_LOGI(TAG, "Clock set from tablet");
    return send_json(req, "200 OK", w);
}

// ── /api/settings ───────────────────────────────────────────────────────────

static esp_err_t send_settings(httpd_req_t* req, const settings::Settings& s) {
    char buf[160];
    JsonWriter w(buf, sizeof(buf));
    settings::write_json(w, s);
    return send_json(req, "200 OK", w);
}

static esp_err_t handle_settings_get(httpd_req_t* req) {
    return send_settings(req, settings_get());
}

static esp_err_t handle_settings_set(httpd_req_t* req) {
    DeviceLock lock;
    if (!api::route_allowed(device_state(), Route::SettingsSet)) return send_busy(req);

    cJSON* body;
    esp_err_t err = read_json_body(req, &body);
    if (!body) return err;

    // Applied to a copy so the update is all-or-nothing
    settings::Settings s = settings_get();
    const char* bad_key = nullptr;
    const char* error   = nullptr;

    const cJSON* item;
    cJSON_ArrayForEach(item, body) {
        const char* key = item->string;
        uint64_t v = 0;
        bool ok;
        if (strcmp(key, "brightness") == 0) {
            ok = cJSON_IsString(item) && settings::parse_brightness(item->valuestring, &s.brightness);
        } else if (strcmp(key, "screen_timeout_s") == 0) {
            ok = get_uint(item, UINT32_MAX, &v) && settings::is_valid_screen_timeout(v);
            s.screen_timeout_s = v;
        } else if (strcmp(key, "default_sample_rate_hz") == 0) {
            ok = get_uint(item, UINT32_MAX, &v) && recording::is_valid_rate(v);
            s.default_rate_hz = v;
        } else if (strcmp(key, "default_duration_s") == 0) {
            ok = get_uint(item, UINT32_MAX, &v) && settings::is_valid_default_duration(v);
            s.default_duration_s = v;
        } else {
            bad_key = key;
            error   = "unknown_setting";
            break;
        }
        if (!ok) {
            bad_key = key;
            error   = "invalid_setting";
            break;
        }
    }

    if (error) {
        err = send_error(req, "400 Bad Request", error, bad_key);
        cJSON_Delete(body);  // bad_key points into body
        return err;
    }
    cJSON_Delete(body);

    if (settings_save(s) != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "storage_error");
    }
    return send_settings(req, s);
}

// ── POST /api/compress ──────────────────────────────────────────────────────

static esp_err_t handle_compress(httpd_req_t* req) {
    if (!api::route_allowed(device_state(), Route::Compress)) return send_busy(req);

    int queued = compressor_start();
    if (queued == COMPRESS_BUSY) return send_busy(req);
    if (queued < 0) return send_error(req, "500 Internal Server Error", "storage_error");

    char buf[32];
    JsonWriter w(buf, sizeof(buf));
    w.begin_object().key("queued").num(queued).end_object();
    return send_json(req, "200 OK", w);
}

// ── POST /api/sleep ─────────────────────────────────────────────────────────

static esp_err_t handle_sleep(httpd_req_t* req) {
    if (!api::route_allowed(device_state(), Route::Sleep)) return send_busy(req);

    char buf[32];
    JsonWriter w(buf, sizeof(buf));
    w.begin_object().key("sleeping").boolean(true).end_object();
    esp_err_t err = send_json(req, "200 OK", w);
    power_deep_sleep_later(500);  // after the response has gone out
    return err;
}

// ── GET /api/calibration ────────────────────────────────────────────────────

static esp_err_t handle_calibration(httpd_req_t* req) {
    if (!api::route_allowed(device_state(), Route::Calibration)) return send_busy(req);
    if (!calibration_available()) {
        return send_error(req, "503 Service Unavailable", "calibration_unavailable");
    }

    // 4096 entries (~20 KB) — streamed in chunks
    char buf[512];
    JsonWriter w(buf, sizeof(buf));
    w.begin_object()
        .key("attenuation_db").num_tenths(OPT101_ADC_ATTEN_TENTHS_DB)
        .key("mv").begin_array();

    httpd_resp_set_type(req, "application/json");
    for (int raw = 0; raw < 4096; raw++) {
        int mv;
        if (!calibration_raw_to_mv(raw, &mv)) return ESP_FAIL;
        w.num(mv);
        if (w.length() > sizeof(buf) - 32) {
            if (httpd_resp_send_chunk(req, w.c_str(), w.length()) != ESP_OK) return ESP_FAIL;
            w.clear_buffer();
        }
    }
    w.end_array().end_object();
    if (!w.ok() || httpd_resp_send_chunk(req, w.c_str(), w.length()) != ESP_OK) return ESP_FAIL;
    return httpd_resp_send_chunk(req, nullptr, 0);
}

// ── Server ──────────────────────────────────────────────────────────────────

void api_start() {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 16;
    cfg.uri_match_fn     = httpd_uri_match_wildcard;  // for /api/recordings/*
    cfg.stack_size       = 8192;                      // cJSON + request body buffer
    cfg.lru_purge_enable = true;                      // recycle sockets the tablet abandoned

    if (httpd_start(&http_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    const httpd_uri_t routes[] = {
        {.uri = "/api/status",       .method = HTTP_GET,    .handler = handle_status,                .user_ctx = nullptr},
        {.uri = "/api/record",       .method = HTTP_POST,   .handler = handle_record_start,          .user_ctx = nullptr},
        {.uri = "/api/record/stop",  .method = HTTP_POST,   .handler = handle_record_stop,           .user_ctx = nullptr},
        {.uri = "/api/recordings",   .method = HTTP_GET,    .handler = handle_recordings_list,       .user_ctx = nullptr},
        {.uri = "/api/recordings",   .method = HTTP_DELETE, .handler = handle_recordings_delete_all, .user_ctx = nullptr},
        {.uri = "/api/recordings/*", .method = HTTP_GET,    .handler = handle_recording_get,         .user_ctx = nullptr},
        {.uri = "/api/recordings/*", .method = HTTP_DELETE, .handler = handle_recording_delete,      .user_ctx = nullptr},
        {.uri = "/api/time",         .method = HTTP_POST,   .handler = handle_time,                  .user_ctx = nullptr},
        {.uri = "/api/settings",     .method = HTTP_GET,    .handler = handle_settings_get,          .user_ctx = nullptr},
        {.uri = "/api/settings",     .method = HTTP_POST,   .handler = handle_settings_set,          .user_ctx = nullptr},
        {.uri = "/api/calibration",  .method = HTTP_GET,    .handler = handle_calibration,           .user_ctx = nullptr},
        {.uri = "/api/compress",     .method = HTTP_POST,   .handler = handle_compress,              .user_ctx = nullptr},
        {.uri = "/api/sleep",        .method = HTTP_POST,   .handler = handle_sleep,                 .user_ctx = nullptr},
    };
    for (const auto& r : routes) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(http_server, &r));
    }

    ESP_LOGI(TAG, "HTTP server started — %u endpoints", (unsigned)(sizeof(routes) / sizeof(routes[0])));
}
