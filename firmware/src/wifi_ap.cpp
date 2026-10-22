#include "wifi_ap.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#include "bootloader_random.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "nvs.h"

#include "config.h"

static const char* TAG = "wifi";

static constexpr const char* KEY_PASSWORD = "wifi_password";

static char ssid[32] = "";
static char password[WIFI_PASSWORD_LEN + 1] = "";

static bool password_new = false;

static std::atomic<int>  clients{0};
static std::atomic<bool> enabled{false};

const char* wifi_ap_ssid()             { return ssid; }
const char* wifi_ap_password()         { return password; }
bool        wifi_ap_client_connected() { return clients.load() > 0; }
bool        wifi_ap_enabled()          { return enabled.load(); }
bool        wifi_ap_password_is_new()  { return password_new; }

// Loads the AP password from NVS, generating one on first boot (spec §8.1)
static void load_or_create_password() {
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h));

    size_t len = sizeof(password);
    if (nvs_get_str(h, KEY_PASSWORD, password, &len) != ESP_OK || strlen(password) < 8) {
        // No look-alike characters (0/o, 1/l/i) since it's typed from the OLED
        static const char charset[] = "abcdefghjkmnpqrstuvwxyz23456789";
        // True RNG before the radio is on; must run before the ADC is in use
        bootloader_random_enable();
        for (int i = 0; i < WIFI_PASSWORD_LEN; i++) {
            password[i] = charset[esp_random() % (sizeof(charset) - 1)];
        }
        bootloader_random_disable();
        password[WIFI_PASSWORD_LEN] = '\0';

        ESP_ERROR_CHECK(nvs_set_str(h, KEY_PASSWORD, password));
        ESP_ERROR_CHECK(nvs_commit(h));
        password_new = true;
        ESP_LOGI(TAG, "Generated new SoftAP password");
    }
    nvs_close(h);
}

static void on_wifi_event(void*, esp_event_base_t, int32_t id, void*) {
    switch (id) {
        case WIFI_EVENT_AP_STACONNECTED:
            clients++;
            ESP_LOGI(TAG, "Client connected");
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            if (clients > 0) clients--;
            ESP_LOGI(TAG, "Client disconnected");
            break;
        case WIFI_EVENT_AP_STOP:
            clients = 0;
            break;
        default:
            break;
    }
}

void wifi_ap_init() {
    load_or_create_password();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, nullptr));

    // Build SSID from last 4 hex chars of MAC
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(ssid, sizeof(ssid), "%s%02X%02X", WIFI_AP_PREFIX, mac[4], mac[5]);

    wifi_config_t ap_cfg = {};
    strlcpy(reinterpret_cast<char*>(ap_cfg.ap.ssid), ssid, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len       = strlen(ssid);
    ap_cfg.ap.channel        = 1;
    ap_cfg.ap.authmode       = WIFI_AUTH_WPA2_PSK;
    strlcpy(reinterpret_cast<char*>(ap_cfg.ap.password), password, sizeof(ap_cfg.ap.password));
    ap_cfg.ap.max_connection = WIFI_MAX_CLIENTS;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(wifi_ap_set_enabled(true));

    // Also on the OLED (first boot, and WiFi → Show SSID & IP)
    ESP_LOGI(TAG, "SoftAP started — SSID: %s, password: %s", ssid, password);
}

esp_err_t wifi_ap_set_enabled(bool on) {
    if (on == enabled.load()) return ESP_OK;
    esp_err_t err = on ? esp_wifi_start() : esp_wifi_stop();
    if (err == ESP_OK) enabled = on;
    return err;
}
