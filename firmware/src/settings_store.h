#pragma once

// User settings persisted in NVS (spec §6.5).

#include "esp_err.h"
#include "settings.h"

// Loads settings from NVS, falling back to defaults for missing or invalid values.
void settings_store_init();

settings::Settings settings_get();

// Validates, persists and makes the settings current.
esp_err_t settings_save(const settings::Settings& s);
