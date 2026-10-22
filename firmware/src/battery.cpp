#include "battery.h"

#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "pins.h"
#include "config.h"
#include "device_state.h"

static const char* TAG = "battery";

static i2c_master_dev_handle_t fuel_dev = nullptr;

// Written by the battery task, read by the HTTP handler and UI
static std::atomic<uint16_t> cell_mv{0};
static std::atomic<uint8_t>  soc_pct{0};

void battery_init(i2c_master_bus_handle_t bus) {
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = I2C_ADDR_MAX17043,
        .scl_speed_hz    = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &cfg, &fuel_dev));
    ESP_LOGI(TAG, "MAX17043 attached (0x%02X)", I2C_ADDR_MAX17043);
}

uint16_t battery_mv()  { return cell_mv.load(); }
uint8_t  battery_pct() { return soc_pct.load(); }

void battery_read_now() {
    const uint8_t reg_vcell = 0x02;
    const uint8_t reg_soc   = 0x04;
    uint8_t buf[2];

    // MAX17043: VCELL register (0x02) — upper 12 bits × 1.25 mV
    if (i2c_master_transmit_receive(fuel_dev, &reg_vcell, 1, buf, 2, 100) == ESP_OK) {
        cell_mv = (((uint16_t)buf[0] << 4) | (buf[1] >> 4)) * 5 / 4;
    }
    // MAX17043: SOC register (0x04) — upper byte = integer percent
    if (i2c_master_transmit_receive(fuel_dev, &reg_soc, 1, buf, 2, 100) == ESP_OK) {
        soc_pct = buf[0];
    }
    ESP_LOGD(TAG, "%u mV, %u%%", cell_mv.load(), soc_pct.load());
}

static void task_battery_monitor(void* arg) {
    for (;;) {
        // Deferred while busy to keep the I2C bus quiet (spec §5.3); the
        // recorder reads right before and after instead
        if (device_state() == api::DeviceState::Idle) battery_read_now();
        vTaskDelay(pdMS_TO_TICKS(BATTERY_READ_INTERVAL_MS));
    }
}

void battery_start_task() {
    // Lowest priority per spec §9.3
    xTaskCreate(task_battery_monitor, "batt", 2048, nullptr, 1, nullptr);
}
