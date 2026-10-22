#pragma once

#include <cstddef>

// LittleFS recording storage (spec §4.1)
void storage_init();

// Cached usage — RAM only, so it's safe to poll during a recording.
void storage_info(size_t* total_bytes, size_t* used_bytes);

// write()/read() until all `len` bytes are done. False on error or early EOF.
bool storage_write_all(int fd, const void* data, size_t len);
bool storage_read_all(int fd, void* data, size_t len);

// Re-reads usage from LittleFS (walks the filesystem, so it reads flash).
// Call after writing or deleting files — never during capture.
void storage_refresh();
