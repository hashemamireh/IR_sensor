#pragma once

#include <cstdint>

// Recording limits, the IREC format and settings defaults live in
// lib/irsensor_core (recording.h, irec.h, settings.h).

// --- OPT101 ADC ---
#define OPT101_ADC_ATTEN    ADC_ATTEN_DB_6  // ~0–2.2 V, matches the OPT101 output ceiling (spec §3.2)
static constexpr int OPT101_ADC_ATTEN_DB = 6;

// --- Recorder (spec §3.3) — tune on hardware ---
static constexpr uint32_t ADC_FRAME_BYTES = 1024;         // 256 results per DMA frame
static constexpr uint32_t ADC_POOL_BYTES  = 16 * 1024;    // ~200 ms at 20 kHz, absorbs WiFi preemption
static constexpr uint32_t SAVE_CHUNK_BYTES = 16 * 1024;   // PSRAM → flash copy size
static constexpr int      RECORDER_CAPTURE_PRIORITY = 18; // above HTTP (5), below the WiFi driver (23)
static constexpr int      RECORDER_SAVE_PRIORITY    = 4;  // below HTTP so status polls answer while saving

// --- Battery monitor ---
static constexpr uint32_t BATTERY_READ_INTERVAL_MS = 30000;  // 30 seconds

// --- Display & input — check on hardware ---
static constexpr int      OLED_HEIGHT = 64;
static constexpr bool     OLED_FLIP   = false;  // rotate 180° if the module is mounted upside down
static constexpr int      ENCODER_COUNTS_PER_DETENT = 4;  // KY-040: one full quadrature cycle per click
static constexpr int      ENCODER_DIRECTION = 1;          // -1 if clockwise moves up the menu
static constexpr uint32_t UI_TICK_MS = 10;

// --- WiFi SoftAP ---
static constexpr const char* WIFI_AP_PREFIX = "IRSensor-";
static constexpr int         WIFI_MAX_CLIENTS = 1;
static constexpr int         WIFI_PASSWORD_LEN = 10;  // generated on first boot

// --- Storage ---
static constexpr const char* LITTLEFS_PARTITION_LABEL = "storage";

// --- NVS ---
static constexpr const char* NVS_NAMESPACE = "irsensor";
