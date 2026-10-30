#pragma once

// Deep sleep and wake-up (spec §7.1). Wake is a GPIO (ext1) wake on the
// encoder button; after boot the firmware requires the press to be held for
// ~2 s, otherwise it goes straight back to sleep.

#include <cstdint>

// Call first in app_main: after a button wake, enforces the 2 s hold.
void power_check_wake();

// Turns off the OLED, LED and WiFi and enters deep sleep. Returns false
// (without sleeping) only if the device is busy.
bool power_deep_sleep();

// For the API: sleeps after `delay_ms`, so the HTTP response goes out first.
void power_deep_sleep_later(uint32_t delay_ms);

// Idle deep sleep (spec §7.1): the API reports activity here; the UI task
// adds encoder input and busy states, and runs the countdown.
void     power_note_activity();
uint32_t power_last_activity_ms();  // esp_timer milliseconds
