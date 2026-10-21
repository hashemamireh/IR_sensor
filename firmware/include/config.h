#pragma once

#include <cstdint>

// --- Recording defaults ---
static constexpr uint32_t DEFAULT_SAMPLE_RATE_HZ = 10000;
static constexpr uint32_t DEFAULT_DURATION_S      = 30;
static constexpr uint32_t MAX_DURATION_S           = 300;

// --- IREC file format ---
static constexpr uint32_t IREC_MAGIC = 0x43455249;  // "IREC" little-endian
static constexpr uint8_t  IREC_VERSION = 0x01;
static constexpr size_t   IREC_HEADER_SIZE = 32;

// --- Battery monitor ---
static constexpr uint32_t BATTERY_READ_INTERVAL_MS = 30000;  // 30 seconds

// --- Display ---
static constexpr uint32_t SCREEN_TIMEOUT_DEFAULT_MS = 60000;  // 1 minute
static constexpr int      OLED_WIDTH  = 128;
static constexpr int      OLED_HEIGHT = 64;

// --- WiFi SoftAP ---
static constexpr const char* WIFI_AP_PREFIX = "IRSensor-";
static constexpr int         WIFI_MAX_CLIENTS = 1;

// --- Storage ---
static constexpr const char* LITTLEFS_PARTITION_LABEL = "storage";
static constexpr const char* RECORDINGS_DIR = "/recordings";

// --- NVS ---
static constexpr const char* NVS_NAMESPACE = "irsensor";
