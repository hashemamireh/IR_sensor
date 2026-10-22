#include "recordings.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs.h"

#include "config.h"
#include "recording.h"

static const char* TAG = "recordings";

static constexpr const char* KEY_NEXT_ID = "next_rec_id";

static std::atomic<uint32_t> count{0};
static uint32_t next_id = 1;

static bool ends_with(const char* s, const char* suffix) {
    size_t n = strlen(s), m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

// Recording IDs present on flash, ascending. False if the directory can't be read.
static bool scan_ids(std::vector<uint32_t>* ids) {
    DIR* dir = opendir(recording::DIR);
    if (!dir) return false;
    while (dirent* e = readdir(dir)) {
        uint32_t id;
        if (recording::parse_filename(e->d_name, &id)) ids->push_back(id);
    }
    closedir(dir);
    std::sort(ids->begin(), ids->end());
    return true;
}

static void remove_temp_files() {
    DIR* dir = opendir(recording::DIR);
    if (!dir) return;
    while (dirent* e = readdir(dir)) {
        if (!ends_with(e->d_name, ".tmp")) continue;
        char path[64];
        snprintf(path, sizeof(path), "%s/%s", recording::DIR, e->d_name);
        ESP_LOGW(TAG, "Removing unfinished recording %s", path);
        unlink(path);
    }
    closedir(dir);
}

void recordings_init() {
    if (mkdir(recording::DIR, 0775) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "Can't create %s: %s", recording::DIR, strerror(errno));
        return;
    }
    remove_temp_files();

    std::vector<uint32_t> ids;
    scan_ids(&ids);
    count = ids.size();

    // Never hand out an ID that's on flash, even if NVS was erased
    uint32_t stored = 1;
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u32(h, KEY_NEXT_ID, &stored);
        nvs_close(h);
    }
    uint32_t after_existing = ids.empty() ? 1 : ids.back() + 1;
    next_id = std::max(stored, after_existing);

    ESP_LOGI(TAG, "%u recordings, next ID %lu", (unsigned)ids.size(), (unsigned long)next_id);
}

uint32_t recordings_count() { return count.load(); }

bool recordings_for_each(const RecordingVisitor& visit) {
    std::vector<uint32_t> ids;
    if (!scan_ids(&ids)) return false;

    for (uint32_t id : ids) {
        char path[48];
        recording::path_for_id(id, path, sizeof(path));
        struct stat st;
        FILE* f = (stat(path, &st) == 0) ? fopen(path, "rb") : nullptr;
        if (!f) continue;

        uint8_t raw[irec::HEADER_SIZE];
        irec::Header h;
        bool valid = fread(raw, 1, sizeof(raw), f) == sizeof(raw) && irec::decode_header(raw, &h);
        fclose(f);

        if (valid) visit(id, h, st.st_size);
        else ESP_LOGW(TAG, "Skipping %s: bad header", path);
    }
    return true;
}

FILE* recordings_open(uint32_t id, uint64_t* size_bytes) {
    char path[48];
    if (!recording::path_for_id(id, path, sizeof(path))) return nullptr;
    struct stat st;
    if (stat(path, &st) != 0) return nullptr;
    *size_bytes = st.st_size;
    return fopen(path, "rb");
}

esp_err_t recordings_delete(uint32_t id) {
    char path[48];
    if (!recording::path_for_id(id, path, sizeof(path))) return ESP_ERR_NOT_FOUND;
    if (unlink(path) != 0) return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    count--;
    ESP_LOGI(TAG, "Deleted %s", path);
    return ESP_OK;
}

uint32_t recordings_delete_all() {
    std::vector<uint32_t> ids;
    scan_ids(&ids);

    uint32_t deleted = 0;
    for (uint32_t id : ids) {
        char path[48];
        recording::path_for_id(id, path, sizeof(path));
        if (unlink(path) == 0) deleted++;
        vTaskDelay(1);  // let lower-priority tasks and the idle task run
    }

    std::vector<uint32_t> left;
    scan_ids(&left);
    count = left.size();
    ESP_LOGI(TAG, "Deleted %lu recordings", (unsigned long)deleted);
    return deleted;
}

uint32_t recordings_allocate_id() {
    if (next_id > recording::MAX_ID) return 0;
    uint32_t id = next_id++;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u32(h, KEY_NEXT_ID, next_id);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    // recordings_init() also skips past existing files, so a lost write only
    // risks reusing the ID of a recording that was since deleted
    if (err != ESP_OK) ESP_LOGW(TAG, "Persisting next ID failed: %s", esp_err_to_name(err));
    return id;
}
