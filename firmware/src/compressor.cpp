#include "compressor.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "miniz.h"

#include "config.h"
#include "device_state.h"
#include "irec.h"
#include "recording.h"
#include "recordings.h"
#include "storage.h"

static const char* TAG = "compressor";

enum class Result { Compressed, Skipped, Failed };

static TaskHandle_t           task = nullptr;
static std::vector<uint32_t>  pending;  // written by compressor_start() before notifying
static std::atomic<uint32_t>  done{0};
static std::atomic<uint32_t>  total{0};

// Large miniz state lives in PSRAM
static tdefl_compressor*   deflator = nullptr;
static tinfl_decompressor* inflator = nullptr;
static uint8_t*            dict     = nullptr;  // TINFL_LZ_DICT_SIZE output window

// Internal RAM: file I/O goes through these, since flash operations disable
// the cache that PSRAM is accessed through
static uint8_t in_buf[COMPRESS_CHUNK_BYTES];
static uint8_t out_buf[COMPRESS_CHUNK_BYTES];

// ── Compress ────────────────────────────────────────────────────────────────

struct Sink {
    int      fd;
    bool     ok;
    uint64_t written;
};

// miniz output callback — its buffer is in PSRAM, so copy before writing flash
static mz_bool put_buf(const void* data, int len, void* user) {
    Sink* sink = static_cast<Sink*>(user);
    const uint8_t* p = static_cast<const uint8_t*>(data);
    while (sink->ok && len > 0) {
        int n = std::min<int>(len, sizeof(out_buf));
        memcpy(out_buf, p, n);
        sink->ok = storage_write_all(sink->fd, out_buf, n);
        sink->written += n;
        p   += n;
        len -= n;
    }
    return sink->ok;
}

// Decompresses the stream in `path` and checks its length and CRC against the
// header, so a bad compressed copy never replaces the original.
static bool verify(const char* path, const irec::Header& h) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    if (lseek(fd, irec::HEADER_SIZE, SEEK_SET) != (off_t)irec::HEADER_SIZE) {
        close(fd);
        return false;
    }

    tinfl_init(inflator);
    size_t   dict_ofs = 0, in_ofs = 0, in_avail = 0;
    uint64_t produced = 0;
    uint32_t crc      = 0;
    bool     eof      = false;
    tinfl_status status;

    for (;;) {
        if (in_avail == 0 && !eof) {
            ssize_t n = read(fd, in_buf, sizeof(in_buf));
            if (n < 0) {
                status = TINFL_STATUS_FAILED;
                break;
            }
            eof      = n == 0;
            in_avail = n;
            in_ofs   = 0;
            vTaskDelay(1);  // yield between chunks
        }
        size_t in_bytes  = in_avail;
        size_t out_bytes = TINFL_LZ_DICT_SIZE - dict_ofs;
        int    flags     = TINFL_FLAG_PARSE_ZLIB_HEADER | (eof ? 0 : TINFL_FLAG_HAS_MORE_INPUT);
        status = tinfl_decompress(inflator, in_buf + in_ofs, &in_bytes, dict, dict + dict_ofs, &out_bytes, flags);

        in_ofs   += in_bytes;
        in_avail -= in_bytes;
        crc       = irec::crc32_update(crc, dict + dict_ofs, out_bytes);
        produced += out_bytes;
        dict_ofs  = (dict_ofs + out_bytes) & (TINFL_LZ_DICT_SIZE - 1);

        if (status <= TINFL_STATUS_DONE) break;  // done or failed
        if (status == TINFL_STATUS_NEEDS_MORE_INPUT && eof) {
            status = TINFL_STATUS_FAILED;  // truncated
            break;
        }
    }
    close(fd);

    uint64_t expected = static_cast<uint64_t>(h.sample_count) * recording::BYTES_PER_SAMPLE;
    return status == TINFL_STATUS_DONE && produced == expected && crc == h.crc32;
}

// Writes <id>.tmp (header with the compressed flag + zlib stream), verifies
// it, then renames it over the original. The CRC stays that of the
// uncompressed samples (spec §4.5).
static Result compress_one(uint32_t id) {
    char path[48], tmp[48];
    recording::path_for_id(id, path, sizeof(path));
    recording::temp_path_for_id(id, tmp, sizeof(tmp));

    int src = open(path, O_RDONLY);
    if (src < 0) return Result::Failed;

    uint8_t      raw[irec::HEADER_SIZE];
    irec::Header h;
    if (!storage_read_all(src, raw, sizeof(raw)) || !irec::decode_header(raw, &h)) {
        close(src);
        ESP_LOGW(TAG, "%s: bad header — skipped", path);
        return Result::Skipped;
    }
    const uint64_t data_bytes = static_cast<uint64_t>(h.sample_count) * recording::BYTES_PER_SAMPLE;
    if ((h.flags & irec::FLAG_COMPRESSED) || data_bytes == 0) {
        close(src);
        return Result::Skipped;
    }

    int dst = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst < 0) {
        close(src);
        return Result::Failed;
    }

    irec::Header out = h;
    out.flags |= irec::FLAG_COMPRESSED;
    irec::encode_header(out, raw);
    Sink sink{dst, storage_write_all(dst, raw, sizeof(raw)), 0};

    bool ok = sink.ok && tdefl_init(deflator, put_buf, &sink, TDEFL_WRITE_ZLIB_HEADER | COMPRESS_PROBES) == TDEFL_STATUS_OKAY;
    uint64_t remaining = data_bytes;
    while (ok && remaining > 0) {
        size_t n = std::min<uint64_t>(sizeof(in_buf), remaining);
        if (!storage_read_all(src, in_buf, n)) {
            ok = false;
            break;
        }
        remaining -= n;
        tdefl_flush    flush  = remaining ? TDEFL_NO_FLUSH : TDEFL_FINISH;
        tdefl_status   expect = remaining ? TDEFL_STATUS_OKAY : TDEFL_STATUS_DONE;
        ok = tdefl_compress_buffer(deflator, in_buf, n, flush) == expect && sink.ok;
        vTaskDelay(1);  // let the idle task run — big files take a while
    }
    close(src);
    ok = (close(dst) == 0) && ok;

    Result result = Result::Failed;
    if (!ok) {
        ESP_LOGE(TAG, "%s: compression failed", path);
    } else if (sink.written >= data_bytes) {
        ESP_LOGI(TAG, "%s: doesn't shrink — left uncompressed", path);
        result = Result::Skipped;
    } else if (!verify(tmp, h)) {
        ESP_LOGE(TAG, "%s: compressed copy failed verification — original kept", path);
    } else if (rename(tmp, path) != 0) {  // LittleFS replaces the original atomically
        ESP_LOGE(TAG, "%s: rename failed — original kept", path);
    } else {
        ESP_LOGI(TAG, "%s: %llu → %llu bytes", path, (unsigned long long)data_bytes,
                 (unsigned long long)sink.written);
        return Result::Compressed;
    }
    unlink(tmp);
    return result;
}

// ── Task ────────────────────────────────────────────────────────────────────

static void task_compressor(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint32_t compressed = 0, skipped = 0, failed = 0;
        for (uint32_t id : pending) {
            switch (compress_one(id)) {
                case Result::Compressed: compressed++; break;
                case Result::Skipped:    skipped++;    break;
                case Result::Failed:     failed++;     break;
            }
            done++;
        }
        storage_refresh();
        ESP_LOGI(TAG, "Done: %lu compressed, %lu skipped, %lu failed", (unsigned long)compressed,
                 (unsigned long)skipped, (unsigned long)failed);

        DeviceLock lock;
        device_set_state(api::DeviceState::Idle);
    }
}

// ── Public API ──────────────────────────────────────────────────────────────

void compressor_init() {
    deflator = static_cast<tdefl_compressor*>(heap_caps_malloc(sizeof(tdefl_compressor), MALLOC_CAP_SPIRAM));
    inflator = static_cast<tinfl_decompressor*>(heap_caps_malloc(sizeof(tinfl_decompressor), MALLOC_CAP_SPIRAM));
    dict     = static_cast<uint8_t*>(heap_caps_malloc(TINFL_LZ_DICT_SIZE, MALLOC_CAP_SPIRAM));
    if (!deflator || !inflator || !dict) {
        ESP_LOGE(TAG, "Can't allocate compressor state in PSRAM — compression disabled");
        heap_caps_free(deflator);
        heap_caps_free(inflator);
        heap_caps_free(dict);
        deflator = nullptr;
        return;
    }
    xTaskCreate(task_compressor, "compress", 4096, nullptr, COMPRESS_PRIORITY, &task);
    ESP_LOGI(TAG, "Ready (%u KB PSRAM)", (unsigned)((sizeof(tdefl_compressor) + sizeof(tinfl_decompressor) + TINFL_LZ_DICT_SIZE) / 1024));
}

int compressor_start() {
    DeviceLock lock(pdMS_TO_TICKS(200));
    if (!lock.held() || device_state() != api::DeviceState::Idle) return COMPRESS_BUSY;
    if (!deflator) return COMPRESS_UNAVAILABLE;

    pending.clear();
    recordings_for_each([](uint32_t id, const irec::Header& h, uint64_t) {
        if (!(h.flags & irec::FLAG_COMPRESSED)) pending.push_back(id);
    });
    if (pending.empty()) return 0;

    done  = 0;
    total = pending.size();
    device_set_state(api::DeviceState::Compressing);
    xTaskNotifyGive(task);
    ESP_LOGI(TAG, "Compressing %u recordings", (unsigned)pending.size());
    return static_cast<int>(pending.size());
}

void compressor_progress(uint32_t* d, uint32_t* t) {
    *d = done.load();
    *t = total.load();
}
