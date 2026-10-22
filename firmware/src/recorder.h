#pragma once

// Recording pipeline (spec §3.3, §9.4): ADC continuous mode + DMA → PSRAM
// buffer during capture, then an IREC file on LittleFS once capture ends.

#include <cstdint>

#include "recording.h"

using recording::StartResult;

// Reserves the PSRAM recording buffer and starts the recorder task.
void recorder_init();

// Starts a recording if the device is idle. On Ok, *id is the new recording's ID.
StartResult recorder_start(uint32_t rate_hz, uint32_t duration_s, uint32_t* id);

// Asks the current capture to stop early. False if nothing is being captured.
bool recorder_stop(uint32_t* id);

// The current (or most recent) recording
struct RecorderProgress {
    uint32_t id;
    uint32_t elapsed_s;
    uint32_t total_s;
    uint32_t rate_hz;
};
RecorderProgress recorder_progress();

// How the most recent recording ended (for the OLED status screen).
recording::Outcome recorder_last_outcome();
