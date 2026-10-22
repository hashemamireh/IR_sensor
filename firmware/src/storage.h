#pragma once

#include <cstddef>

// LittleFS recording storage (spec §4.1)
void storage_init();
void storage_info(size_t* total_bytes, size_t* used_bytes);
