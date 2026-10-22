#include "device_state.h"

#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t lock = nullptr;

// Readable without the lock (status polling); written with it held
static std::atomic<api::DeviceState> state{api::DeviceState::Idle};

void device_state_init() {
    lock = xSemaphoreCreateMutex();
    configASSERT(lock);
}

api::DeviceState device_state()           { return state.load(); }
void device_set_state(api::DeviceState s) { state = s; }

DeviceLock::DeviceLock(TickType_t timeout) : held_(xSemaphoreTake(lock, timeout) == pdTRUE) {}

DeviceLock::~DeviceLock() {
    if (held_) xSemaphoreGive(lock);
}
