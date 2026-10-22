#include "api.h"

#include "esp_http_server.h"
#include "esp_log.h"

#include "api_model.h"
#include "battery.h"
#include "json_writer.h"
#include "storage.h"
#include "wifi_ap.h"

static const char* TAG = "api";

static httpd_handle_t http_server = nullptr;

static esp_err_t handle_status(httpd_req_t* req) {
    size_t total = 0, used = 0;
    storage_info(&total, &used);

    api::StatusInfo s;
    s.battery_mv  = battery_mv();
    s.battery_pct = battery_pct();
    s.free_bytes  = total - used;
    s.ssid        = wifi_ap_ssid();
    // ponytail: state, recording count and client_connected are filled in
    // as the recorder, recording index and WiFi events land

    char buf[320];
    JsonWriter w(buf, sizeof(buf));
    api::write_status(w, s);
    if (!w.ok()) return httpd_resp_send_500(req);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, w.c_str(), w.length());
}

void api_start() {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&http_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    // GET /api/status
    httpd_uri_t status_uri = {
        .uri      = "/api/status",
        .method   = HTTP_GET,
        .handler  = handle_status,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(http_server, &status_uri);

    // ponytail: remaining endpoints (record, recordings, time, etc.) are
    // registered here as they're implemented. One handler per endpoint,
    // no router abstraction needed — esp_http_server already is one.

    ESP_LOGI(TAG, "HTTP server started — GET /api/status registered");
}
