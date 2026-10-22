#include "wifi_ap.h"

#include <cstdio>
#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "config.h"

static const char* TAG = "wifi";

void wifi_ap_init() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init));

    // Build SSID from last 4 hex chars of MAC
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "%s%02X%02X", WIFI_AP_PREFIX, mac[4], mac[5]);

    wifi_config_t ap_cfg = {};
    strlcpy(reinterpret_cast<char*>(ap_cfg.ap.ssid), ssid, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len       = strlen(ssid);
    ap_cfg.ap.channel        = 1;
    ap_cfg.ap.authmode       = WIFI_AUTH_WPA2_PSK;
    // ponytail: hardcoded until the generated, NVS-stored password lands
    strlcpy(reinterpret_cast<char*>(ap_cfg.ap.password), "irsensor1234", sizeof(ap_cfg.ap.password));
    ap_cfg.ap.max_connection = WIFI_MAX_CLIENTS;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "SoftAP started — SSID: %s", ssid);
}
