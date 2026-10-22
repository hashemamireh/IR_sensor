#pragma once

#include <cstdint>

// Recording limits, the IREC format and settings defaults live in
// lib/irsensor_core (recording.h, irec.h, settings.h).

// --- Battery monitor ---
static constexpr uint32_t BATTERY_READ_INTERVAL_MS = 30000;  // 30 seconds

// --- Display ---
static constexpr int      OLED_WIDTH  = 128;
static constexpr int      OLED_HEIGHT = 64;

// --- WiFi SoftAP ---
static constexpr const char* WIFI_AP_PREFIX = "IRSensor-";
static constexpr int         WIFI_MAX_CLIENTS = 1;

// --- Storage ---
static constexpr const char* LITTLEFS_PARTITION_LABEL = "storage";

// --- NVS ---
static constexpr const char* NVS_NAMESPACE = "irsensor";
