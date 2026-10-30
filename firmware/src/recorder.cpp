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
#include "live.h"
#include "pins.h"
#include "recording.h"
#include "recordings.h"
#include "storage.h"
#include "synthetic.h"

static const char* TAG = "recorder";

enum class JobKind : uint8_t { Record, Live };

struct Job {
    JobKind  kind;
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
static std::atomic<uint32_t> pool_overflows{0};  // running count, for live view
static recording::Outcome    last_outcome;
static portMUX_TYPE          outcome_lock = portMUX_INITIALIZER_UNLOCKED;

// Internal RAM: DMA results are parsed here, and the save copies through
// here because flash writes disable the cache that PSRAM is read through
static uint8_t               frame[ADC_FRAME_BYTES];
static adc_continuous_data_t parsed[ADC_FRAME_BYTES / SOC_ADC_DIGI_RESULT_BYTES];
static uint8_t               staging[SAVE_CHUNK_BYTES];

// Live view (spec §3.6)
static live::Ring            ring;
static portMUX_TYPE          ring_lock = portMUX_INITIALIZER_UNLOCKED;
static std::atomic<bool>     live_stop_requested{false};
static uint32_t              live_overrun_base = 0;

// ── Capture ─────────────────────────────────────────────────────────────────

static bool IRAM_ATTR on_conv_done(adc_continuous_handle_t, const adc_continuous_evt_data_t*, void*) {
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(task, &woken);
    return woken == pdTRUE;
}

static bool IRAM_ATTR on_pool_ovf(adc_continuous_handle_t, const adc_continuous_evt_data_t*, void*) {
    pool_overflowed = true;
    pool_overflows.fetch_add(1);
    return false;
}

struct AdcRun {
    bool     started;
    bool     timed_out;
    uint32_t invalid;
};

#ifndef IRSENSOR_SYNTHETIC_ADC

// Runs the ADC at `rate_hz` and hands every sample to `sink` until the sink
// returns false, `stop` is set, or `deadline_us` passes (0 = no deadline).
// Shared by recording and live view.
template <typename Sink>
static AdcRun run_adc(uint32_t rate_hz, const std::atomic<bool>& stop, int64_t deadline_us, Sink&& sink) {
    AdcRun r{false, false, 0};

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
        .sample_freq_hz = rate_hz,
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
        return r;
    }
    r.started = true;

    bool more = true;
    while (more && !stop) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));

        // Drain every frame that's ready
        uint32_t len = 0;
        while (more && adc_continuous_read(adc, frame, sizeof(frame), &len, 0) == ESP_OK) {
            uint32_t n = 0;
            adc_continuous_parse_data(adc, frame, len, parsed, &n);
            for (uint32_t i = 0; i < n && more; i++) {
                if (parsed[i].valid) more = sink(static_cast<uint16_t>(parsed[i].raw_data));
                else r.invalid++;
            }
        }
        if (deadline_us && esp_timer_get_time() > deadline_us) {
            r.timed_out = true;
            break;
        }
    }

    adc_continuous_stop(adc);
    adc_continuous_deinit(adc);
    return r;
}

#else  // IRSENSOR_SYNTHETIC_ADC

// Generates the synthetic signal at `rate_hz` instead of reading the ADC
template <typename Sink>
static AdcRun run_adc(uint32_t rate_hz, const std::atomic<bool>& stop, int64_t deadline_us, Sink&& sink) {
    (void)deadline_us;
    const int64_t t0   = esp_timer_get_time();
    uint64_t      next = 0;
    bool          more = true;
    pool_overflowed    = false;
    while (more && !stop) {
        vTaskDelay(pdMS_TO_TICKS(10));
        uint64_t due = static_cast<uint64_t>(esp_timer_get_time() - t0) * rate_hz / 1000000;
        while (more && next < due) more = sink(synthetic::sample(next++, rate_hz));
    }
    return {true, false, 0};
}

#endif

static Capture capture(const Job& job) {
    const uint32_t target = job.rate_hz * job.duration_s;
    Capture c{0, false};
    const int64_t deadline = esp_timer_get_time() + (int64_t)(job.duration_s + 2) * 1000000;

    AdcRun r = run_adc(job.rate_hz, stop_requested, deadline, [&](uint16_t v) {
        buffer[c.count++] = v;
        samples_captured  = c.count;
        return c.count < target;
    });
    if (!r.started) return c;

    if (r.timed_out) {
        ESP_LOGE(TAG, "ADC delivered %lu of %lu samples in time — stopping",
                 (unsigned long)c.count, (unsigned long)target);
    }
    if (r.invalid) ESP_LOGW(TAG, "%lu invalid ADC results dropped", (unsigned long)r.invalid);
    c.overrun = pool_overflowed || r.invalid > 0;
    return c;
}

// Live view: full-rate sampling reduced to min/max/mean buckets in the ring,
// until live_stop() (spec §3.6). Nothing is stored.
static void run_live() {
    taskENTER_CRITICAL(&ring_lock);
    ring.clear();
    taskEXIT_CRITICAL(&ring_lock);
    live_overrun_base = pool_overflows.load();
    ESP_LOGI(TAG, "Live view started (%lu Hz, %lu ms buckets)", (unsigned long)live::SAMPLE_RATE_HZ,
             (unsigned long)live::BUCKET_MS);

    live::Accumulator acc;
    run_adc(live::SAMPLE_RATE_HZ, live_stop_requested, 0, [&](uint16_t v) {
        if (acc.add(v)) {
            live::Bucket b = acc.take();
            taskENTER_CRITICAL(&ring_lock);
            ring.push(b);
            taskEXIT_CRITICAL(&ring_lock);
        }
        return true;
    });

    ESP_LOGI(TAG, "Live view stopped");
    DeviceLock lock;
    device_set_state(api::DeviceState::Idle);
}

// ── Save ────────────────────────────────────────────────────────────────────

// Writes the IREC file to a temp path, then renames it into place (spec §9.4).
// On success *out holds the header written, for the recordings index.
static bool save(const Job& job, uint64_t start_utc_us, const Capture& c, irec::Header* out) {
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
    *out = h;
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
    irec::Header header;
    bool saved = save(job, start_utc_us, c, &header);
    storage_refresh();
    vTaskPrioritySet(nullptr, RECORDER_CAPTURE_PRIORITY);

    battery_read_now();  // reading right after the recording (spec §5.3)
    {
        DeviceLock lock;
        if (saved) {
            uint64_t size = irec::HEADER_SIZE + static_cast<uint64_t>(c.count) * recording::BYTES_PER_SAMPLE;
            recordings_put(job.id, header, size);
        }
        taskENTER_CRITICAL(&outcome_lock);
        last_outcome = {true, job.id, saved, c.overrun, c.count, job.rate_hz};
        taskEXIT_CRITICAL(&outcome_lock);
        device_set_state(api::DeviceState::Idle);  // the UI shows the summary
    }
}

static void task_recorder(void*) {
    Job job;
    for (;;) {
        if (xQueueReceive(jobs, &job, portMAX_DELAY) != pdTRUE) continue;
        if (job.kind == JobKind::Live) run_live();
        else run(job);
    }
}

// Asks live view to end and waits for the ADC to be released
static bool end_live(uint32_t timeout_ms) {
    if (device_state() != api::DeviceState::Live) return true;
    live_stop_requested = true;
    for (uint32_t waited = 0; waited < timeout_ms; waited += 10) {
        if (device_state() != api::DeviceState::Live) return true;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
}

// ── Public API ──────────────────────────────────────────────────────────────

void recorder_init() {
    jobs = xQueueCreate(1, sizeof(Job));
    xTaskCreate(task_recorder, "recorder", 4096, nullptr, RECORDER_CAPTURE_PRIORITY, &task);

    const size_t bytes = recording::max_buffer_bytes();
    buffer = static_cast<uint16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM));
    if (!buffer) {
        ESP_LOGE(TAG, "Can't reserve %u bytes of PSRAM — recording disabled", (unsigned)bytes);
        return;
    }

#ifdef IRSENSOR_SYNTHETIC_ADC
    ESP_LOGW(TAG, "SYNTHETIC build — recordings contain generated samples, not ADC data");
#endif
    ESP_LOGI(TAG, "Reserved %u KB of PSRAM for recordings", (unsigned)(bytes / 1024));
}

StartResult recorder_start(uint32_t rate_hz, uint32_t duration_s, uint32_t* id) {
    // Recording from live view ends it first (spec §3.6)
    if (!end_live(1000)) return StartResult::Busy;

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

    current          = {JobKind::Record, new_id, rate_hz, duration_s};
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

// ── Live view ───────────────────────────────────────────────────────────────

bool live_start() {
    DeviceLock lock(pdMS_TO_TICKS(200));
    if (!lock.held() || device_state() != api::DeviceState::Idle) return false;
    live_stop_requested = false;
    device_set_state(api::DeviceState::Live);
    Job job{JobKind::Live, 0, 0, 0};
    xQueueSend(jobs, &job, 0);
    return true;
}

void live_stop() {
    if (device_state() == api::DeviceState::Live) live_stop_requested = true;
}

bool live_stop_and_wait() { return end_live(1000); }

size_t live_read(uint64_t since, live::Bucket* out, size_t max, uint64_t* first, uint64_t* next) {
    taskENTER_CRITICAL(&ring_lock);
    size_t n = ring.read(since, out, max, first);
    *next = ring.next_seq();
    taskEXIT_CRITICAL(&ring_lock);
    return n;
}

size_t live_latest(live::Bucket* out, size_t max) {
    uint64_t first;
    taskENTER_CRITICAL(&ring_lock);
    size_t n = ring.latest(out, max, &first);
    taskEXIT_CRITICAL(&ring_lock);
    return n;
}

uint32_t live_overruns() { return pool_overflows.load() - live_overrun_base; }
