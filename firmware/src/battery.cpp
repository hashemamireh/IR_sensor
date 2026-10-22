#include "battery.h"

#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "pins.h"
#include "config.h"
#include "device_state.h"

static const char* TAG = "battery";

static constexpr uint8_t REG_VCELL    = 0x02;
static constexpr uint8_t REG_SOC      = 0x04;
static constexpr uint8_t REG_CONFIG   = 0x0C;  // MSB: RCOMP, LSB: SLEEP | ALRT | ATHD
static constexpr uint8_t CONFIG_SLEEP = 0x80;  // LSB bit 7

// After sleep the gauge needs a moment before VCELL/SOC are fresh
static constexpr uint32_t FIRST_READ_DELAY_MS = 1000;

static i2c_master_dev_handle_t fuel_dev = nullptr;

// Written by the battery task, read by the HTTP handler and UI
static std::atomic<uint16_t> cell_mv{0};
static std::atomic<uint8_t>  soc_pct{0};

// Sets or clears CONFIG.SLEEP, keeping RCOMP and the alert settings
static esp_err_t set_sleep(bool sleep) {
    uint8_t cfg[2];
    esp_err_t err = i2c_master_transmit_receive(fuel_dev, &REG_CONFIG, 1, cfg, 2, 100);
    if (err != ESP_OK) return err;
    uint8_t lsb = sleep ? (cfg[1] | CONFIG_SLEEP) : (cfg[1] & ~CONFIG_SLEEP);
    const uint8_t write[3] = {REG_CONFIG, cfg[0], lsb};
    return i2c_master_transmit(fuel_dev, write, sizeof(write), 100);
}

void battery_init(i2c_master_bus_handle_t bus) {
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = I2C_ADDR_MAX17043,
        .scl_speed_hz    = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &cfg, &fuel_dev));

    // Clear SLEEP in case we're waking from deep sleep (a power cycle clears it anyway)
    if (set_sleep(false) == ESP_OK) ESP_LOGI(TAG, "MAX17043 ready (0x%02X)", I2C_ADDR_MAX17043);
    else ESP_LOGW(TAG, "MAX17043 not responding (0x%02X)", I2C_ADDR_MAX17043);
}

void battery_sleep() {
    esp_err_t err = set_sleep(true);
    if (err != ESP_OK) ESP_LOGW(TAG, "Couldn't put MAX17043 to sleep: %s", esp_err_to_name(err));
}

uint16_t battery_mv()  { return cell_mv.load(); }
uint8_t  battery_pct() { return soc_pct.load(); }

void battery_read_now() {
    uint8_t buf[2];

    // MAX17043: VCELL register — upper 12 bits × 1.25 mV
    if (i2c_master_transmit_receive(fuel_dev, &REG_VCELL, 1, buf, 2, 100) == ESP_OK) {
        cell_mv = (((uint16_t)buf[0] << 4) | (buf[1] >> 4)) * 5 / 4;
    }
    // MAX17043: SOC register — upper byte = integer percent
    if (i2c_master_transmit_receive(fuel_dev, &REG_SOC, 1, buf, 2, 100) == ESP_OK) {
        soc_pct = buf[0];
    }
    ESP_LOGD(TAG, "%u mV, %u%%", cell_mv.load(), soc_pct.load());
}

static void task_battery_monitor(void* arg) {
    vTaskDelay(pdMS_TO_TICKS(FIRST_READ_DELAY_MS));
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
