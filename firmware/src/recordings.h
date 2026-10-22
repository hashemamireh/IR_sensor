#pragma once

// Recording files on LittleFS (spec §4.6): index, listing, deletion and IDs.
// Every recording's header is kept in RAM (read from flash once, at boot), so
// listing and counting never touch flash. Callers hold DeviceLock around
// changes, so they never overlap a recording.

#include <cstdint>
#include <cstdio>
#include <functional>

#include "esp_err.h"
#include "irec.h"

// Creates the recordings directory, removes temp files left by an interrupted
// save, counts recordings and syncs the ID counter.
void recordings_init();

uint32_t recordings_count();

using RecordingVisitor =
    std::function<void(uint32_t id, const irec::Header& h, uint64_t size_bytes)>;

// Visits recordings with a valid header in ascending ID order — from RAM.
void recordings_for_each(const RecordingVisitor& visit);

// Opens a recording for reading; nullptr if it doesn't exist.
FILE* recordings_open(uint32_t id, uint64_t* size_bytes);

// ESP_ERR_NOT_FOUND if there's no such recording.
esp_err_t recordings_delete(uint32_t id);

// Returns the number of recordings deleted.
uint32_t recordings_delete_all();

// Adds or replaces a recording in the index — after the recorder saves a new
// file, or the compressor rewrites one.
void recordings_put(uint32_t id, const irec::Header& h, uint64_t size_bytes);

// Next unused recording ID, persisted in NVS. 0 when IDs are exhausted.
uint32_t recordings_allocate_id();
