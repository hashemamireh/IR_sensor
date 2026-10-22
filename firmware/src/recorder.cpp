#include "recorder.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_adc/adc_continuous.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "battery.h"
#include "config.h"
#include "device_state.h"
#include "irec.h"
#include "pins.h"
#include "recording.h"
#include "recordings.h"
#include "storage.h"
#include "synthetic.h"

static const char* TAG = "recorder";

struct Job {
    uint32_t id;
    uint32_t rate_hz;
    uint32_t duration_s;
};

struct Capture {
    uint32_t count;    // samples in the buffer
    bool     overrun;  // samples were lost — timing unreliable
};

static uint16_t*     buffer = nullptr;  // PSRAM, sized for the longest recording
static TaskHandle_t  task   = nullptr;
static QueueHandle_t jobs   = nullptr;

static Job                   current{};  // written before the job is queued
static std::atomic<bool>     stop_requested{false};
static std::atomic<uint32_t> samples_captured{0};
static std::atomic<bool>     pool_overflowed{false};
static recording::Outcome    last_outcome;
static portMUX_TYPE          outcome_lock = portMUX_INITIALIZER_UNLOCKED;

// Internal RAM: DMA results are parsed here, and the save copies through
// here because flash writes disable the cache that PSRAM is read through
static uint8_t               frame[ADC_FRAME_BYTES];
static adc_continuous_data_t parsed[ADC_FRAME_BYTES / SOC_ADC_DIGI_RESULT_BYTES];
static uint8_t               staging[SAVE_CHUNK_BYTES];

// ── Capture ─────────────────────────────────────────────────────────────────

static bool IRAM_ATTR on_conv_done(adc_continuous_handle_t, const adc_continuous_evt_data_t*, void*) {
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(task, &woken);
    return woken == pdTRUE;
}

static bool IRAM_ATTR on_pool_ovf(adc_continuous_handle_t, const adc_continuous_evt_data_t*, void*) {
    pool_overflowed = true;
    return false;
}

#ifndef IRSENSOR_SYNTHETIC_ADC

static Capture capture(const Job& job) {
    const uint32_t target = job.rate_hz * job.duration_s;
    Capture c{0, false};

    adc_continuous_handle_t adc = nullptr;
    adc_continuous_handle_cfg_t handle_cfg = {
        .max_store_buf_size = ADC_POOL_BYTES,
        .conv_frame_size    = ADC_FRAME_BYTES,
        .flags = { .flush_pool = 1 },
    };
    adc_digi_pattern_config_t pattern = {
        .atten     = OPT101_ADC_ATTEN,
        .channel   = OPT101_ADC_CHANNEL,
        .unit      = OPT101_ADC_UNIT,
        .bit_width = ADC_BITWIDTH_12,
    };
    adc_continuous_config_t cfg = {
        .pattern_num    = 1,
        .adc_pattern    = &pattern,
        .sample_freq_hz = job.rate_hz,
        .conv_mode      = ADC_CONV_SINGLE_UNIT_1,
        .format         = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
    };
    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = on_conv_done,
        .on_pool_ovf  = on_pool_ovf,
    };

    esp_err_t err = adc_continuous_new_handle(&handle_cfg, &adc);
    if (err == ESP_OK) err = adc_continuous_config(adc, &cfg);
    if (err == ESP_OK) err = adc_continuous_register_event_callbacks(adc, &cbs, nullptr);
    if (err == ESP_OK) {
        pool_overflowed = false;
        ulTaskNotifyTake(pdTRUE, 0);  // drop any stale notification
        err = adc_continuous_start(adc);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC setup failed: %s", esp_err_to_name(err));
        if (adc) adc_continuous_deinit(adc);
        return c;
    }

    uint32_t invalid = 0;
    const int64_t deadline = esp_timer_get_time() + (int64_t)(job.duration_s + 2) * 1000000;

    while (c.count < target && !stop_requested) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));

        // Drain every frame that's ready
        uint32_t len = 0;
        while (c.count < target && adc_continuous_read(adc, frame, sizeof(frame), &len, 0) == ESP_OK) {
            uint32_t n = 0;
            adc_continuous_parse_data(adc, frame, len, parsed, &n);
            for (uint32_t i = 0; i < n && c.count < target; i++) {
                if (parsed[i].valid) buffer[c.count++] = static_cast<uint16_t>(parsed[i].raw_data);
                else invalid++;
            }
        }
        samples_captured = c.count;

        if (esp_timer_get_time() > deadline) {
            ESP_LOGE(TAG, "ADC delivered %lu of %lu samples in time — stopping",
                     (unsigned long)c.count, (unsigned long)target);
            break;
        }
    }

    adc_continuous_stop(adc);
    adc_continuous_deinit(adc);

    if (invalid) ESP_LOGW(TAG, "%lu invalid ADC results dropped", (unsigned long)invalid);
    c.overrun = pool_overflowed || invalid > 0;
    return c;
}

#else  // IRSENSOR_SYNTHETIC_ADC

// Generates samples at the requested rate instead of reading the ADC
static Capture capture(const Job& job) {
    const uint32_t target = job.rate_hz * job.duration_s;
    Capture c{0, false};
    const int64_t t0 = esp_timer_get_time();

    while (c.count < target && !stop_requested) {
        vTaskDelay(pdMS_TO_TICKS(10));
        uint64_t due = static_cast<uint64_t>(esp_timer_get_time() - t0) * job.rate_hz / 1000000;
        while (c.count < target && c.count < due) {
            buffer[c.count] = synthetic::sample(c.count, job.rate_hz);
            c.count++;
        }
        samples_captured = c.count;
    }
    return c;
}

#endif

// ── Save ────────────────────────────────────────────────────────────────────

// Writes the IREC file to a temp path, then renames it into place (spec §9.4)
static bool save(const Job& job, uint64_t start_utc_us, const Capture& c) {
    if (c.count == 0) {
        ESP_LOGW(TAG, "No samples captured — nothing saved");
        return false;
    }
    const size_t   data_bytes = static_cast<size_t>(c.count) * recording::BYTES_PER_SAMPLE;
    const uint8_t* data       = reinterpret_cast<const uint8_t*>(buffer);

    // In-memory uint16 on little-endian RISC-V is already the uint16_le[] file layout
    irec::Header h;
    h.sample_rate_mhz = recording::actual_rate_mhz(job.rate_hz);
    h.sample_count    = c.count;
    h.start_utc_us    = start_utc_us;
    h.crc32           = irec::crc32_update(0, data, data_bytes);
    h.flags           = c.overrun ? irec::FLAG_OVERRUN : 0;
    h.atten_tenths_db = OPT101_ADC_ATTEN_TENTHS_DB;

    uint8_t header[irec::HEADER_SIZE];
    irec::encode_header(h, header);

    char tmp_path[48], final_path[48];
    recording::temp_path_for_id(job.id, tmp_path, sizeof(tmp_path));
    recording::path_for_id(job.id, final_path, sizeof(final_path));

    int fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        ESP_LOGE(TAG, "Can't create %s: %s", tmp_path, strerror(errno));
        return false;
    }

    bool ok = storage_write_all(fd, header, sizeof(header));
    for (size_t off = 0; ok && off < data_bytes; off += SAVE_CHUNK_BYTES) {
        size_t n = std::min<size_t>(SAVE_CHUNK_BYTES, data_bytes - off);
        memcpy(staging, data + off, n);
        ok = storage_write_all(fd, staging, n);
        vTaskDelay(1);  // let the idle task run — a 6 MB save takes several seconds
    }
    ok = (close(fd) == 0) && ok;
    if (ok) ok = rename(tmp_path, final_path) == 0;

    if (!ok) {
        ESP_LOGE(TAG, "Saving %s failed: %s", final_path, strerror(errno));
        unlink(tmp_path);
        return false;
    }
    ESP_LOGI(TAG, "Saved %s — %lu samples, CRC %08lx%s", final_path, (unsigned long)c.count,
             (unsigned long)h.crc32, c.overrun ? " (OVERRUN: samples lost)" : "");
    return true;
}

// ── Task ────────────────────────────────────────────────────────────────────

static void run(const Job& job) {
    // The UI task follows the state: it freezes the OLED on the recording
    // screen and pulses the LED red (spec §6.2)
    timeval tv;
    gettimeofday(&tv, nullptr);
    const uint64_t start_utc_us = static_cast<uint64_t>(tv.tv_sec) * 1000000 + tv.tv_usec;

    const int64_t t0 = esp_timer_get_time();
    Capture c = capture(job);
    const int64_t elapsed_us = esp_timer_get_time() - t0;

    // Rough measured rate — the numbers for the §3.5 actual-rate table
    if (elapsed_us > 0) {
        ESP_LOGI(TAG, "Captured %lu samples in %.3f s — ~%.1f Hz (nominal %lu Hz)",
                 (unsigned long)c.count, elapsed_us / 1e6, c.count * 1e6 / elapsed_us,
                 (unsigned long)job.rate_hz);
    }

    device_set_state(api::DeviceState::Saving);
    vTaskPrioritySet(nullptr, RECORDER_SAVE_PRIORITY);
    bool saved = save(job, start_utc_us, c);
    storage_refresh();
    vTaskPrioritySet(nullptr, RECORDER_CAPTURE_PRIORITY);

    battery_read_now();  // reading right after the recording (spec §5.3)
    {
        DeviceLock lock;
        if (saved) recordings_added();
        taskENTER_CRITICAL(&outcome_lock);
        last_outcome = {true, job.id, saved, c.overrun, c.count, job.rate_hz};
        taskEXIT_CRITICAL(&outcome_lock);
        device_set_state(api::DeviceState::Idle);  // the UI shows the summary
    }
}

static void task_recorder(void*) {
    Job job;
    for (;;) {
        if (xQueueReceive(jobs, &job, portMAX_DELAY) == pdTRUE) run(job);
    }
}

// ── Public API ──────────────────────────────────────────────────────────────

void recorder_init() {
    const size_t bytes = recording::max_buffer_bytes();
    buffer = static_cast<uint16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM));
    if (!buffer) {
        ESP_LOGE(TAG, "Can't reserve %u bytes of PSRAM — recording disabled", (unsigned)bytes);
        return;
    }
    jobs = xQueueCreate(1, sizeof(Job));
    xTaskCreate(task_recorder, "recorder", 4096, nullptr, RECORDER_CAPTURE_PRIORITY, &task);

#ifdef IRSENSOR_SYNTHETIC_ADC
    ESP_LOGW(TAG, "SYNTHETIC build — recordings contain generated samples, not ADC data");
#endif
    ESP_LOGI(TAG, "Reserved %u KB of PSRAM for recordings", (unsigned)(bytes / 1024));
}

StartResult recorder_start(uint32_t rate_hz, uint32_t duration_s, uint32_t* id) {
    // Short wait: a download in progress holds the lock for the whole transfer
    DeviceLock lock(pdMS_TO_TICKS(200));
    if (!lock.held() || device_state() != api::DeviceState::Idle) return StartResult::Busy;
    if (!buffer) return StartResult::StorageError;

    size_t total = 0, used = 0;
    storage_info(&total, &used);
    switch (recording::validate(rate_hz, duration_s, total - used)) {
        case recording::Error::InvalidRate:       return StartResult::InvalidRate;
        case recording::Error::InvalidDuration:   return StartResult::InvalidDuration;
        case recording::Error::InsufficientSpace: return StartResult::InsufficientSpace;
        case recording::Error::None:              break;
    }

    uint32_t new_id = recordings_allocate_id();
    if (new_id == 0) return StartResult::StorageError;

    battery_read_now();  // last reading before battery reads pause (spec §5.3)

    current          = {new_id, rate_hz, duration_s};
    samples_captured = 0;
    stop_requested   = false;
    device_set_state(api::DeviceState::Recording);
    xQueueSend(jobs, &current, 0);

    ESP_LOGI(TAG, "Recording %lu: %lu s at %lu Hz", (unsigned long)new_id,
             (unsigned long)duration_s, (unsigned long)rate_hz);
    *id = new_id;
    return StartResult::Ok;
}

bool recorder_stop(uint32_t* id) {
    if (device_state() != api::DeviceState::Recording) return false;
    *id = current.id;
    stop_requested = true;
    return true;
}

RecorderProgress recorder_progress() {
    uint32_t rate = current.rate_hz ? current.rate_hz : 1;
    return {current.id, samples_captured.load() / rate, current.duration_s, current.rate_hz};
}

recording::Outcome recorder_last_outcome() {
    taskENTER_CRITICAL(&outcome_lock);
    recording::Outcome o = last_outcome;
    taskEXIT_CRITICAL(&outcome_lock);
    return o;
}
