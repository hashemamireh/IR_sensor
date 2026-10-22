#pragma once

#include "esp_err.h"

// WiFi SoftAP (spec §8.1)
void        wifi_ap_init();
const char* wifi_ap_ssid();
const char* wifi_ap_password();
bool        wifi_ap_password_is_new();  // generated this boot — show it on the OLED
bool        wifi_ap_client_connected();

// WiFi menu: Enable / Disable AP (spec §6.4)
esp_err_t wifi_ap_set_enabled(bool enabled);
bool      wifi_ap_enabled();
