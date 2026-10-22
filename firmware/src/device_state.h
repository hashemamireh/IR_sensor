#pragma once

// Current device state (spec §8.2) and the lock that keeps flash work by the
// HTTP API, the UI and the recorder from overlapping.

#include "api_model.h"

void             device_state_init();
api::DeviceState device_state();
void             device_set_state(api::DeviceState s);

// Holds the device lock for its lifetime. Take it before checking the state
// and keep it while doing flash work (files, NVS) or changing the state.
class DeviceLock {
public:
    DeviceLock();
    ~DeviceLock();
    DeviceLock(const DeviceLock&) = delete;
    DeviceLock& operator=(const DeviceLock&) = delete;
};
