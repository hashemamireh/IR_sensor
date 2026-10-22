#include "api.h"

#include <cstdio>

#include "esp_http_server.h"
#include "esp_log.h"

#include "battery.h"
#include "storage.h"

static const char* TAG = "api";

static httpd_handle_t http_server = nullptr;

static esp_err_t handle_status(httpd_req_t* req) {
    size_t total = 0, used = 0;
    storage_info(&total, &used);

    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"state\":\"idle\",\"battery_pct\":%u,\"battery_mv\":%u,\"free_kb\":%zu}",
             battery_pct(), battery_mv(), (total - used) / 1024);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
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
