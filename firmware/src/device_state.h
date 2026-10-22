#pragma once

// Current device state (spec §8.2) and the lock that keeps flash work by the
// HTTP API, the UI and the recorder from overlapping.

#include "freertos/FreeRTOS.h"

#include "api_model.h"

void             device_state_init();
api::DeviceState device_state();
void             device_set_state(api::DeviceState s);

// Holds the device lock for its lifetime. Take it before checking the state
// and keep it while doing flash work (files, NVS) or changing the state.
// With a timeout, check held() before relying on it.
class DeviceLock {
public:
    explicit DeviceLock(TickType_t timeout = portMAX_DELAY);
    ~DeviceLock();
    DeviceLock(const DeviceLock&) = delete;
    DeviceLock& operator=(const DeviceLock&) = delete;

    bool held() const { return held_; }

private:
    bool held_;
};
