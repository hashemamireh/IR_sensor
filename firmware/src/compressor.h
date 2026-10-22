#pragma once

// Compress All (spec §4.4): zlib-compresses the samples of every uncompressed
// recording in a low-priority background task, using the ROM's miniz.

#include <cstdint>

constexpr int COMPRESS_BUSY        = -1;
constexpr int COMPRESS_UNAVAILABLE = -2;

void compressor_init();

// Queues every uncompressed recording and sets the state to Compressing.
// Returns the number queued (0 = nothing to do), or COMPRESS_BUSY / COMPRESS_UNAVAILABLE.
int compressor_start();

void compressor_progress(uint32_t* done, uint32_t* total);
