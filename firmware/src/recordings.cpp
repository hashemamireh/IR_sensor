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
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs.h"

#include "config.h"
#include "recording.h"
#include "storage.h"

static const char* TAG = "recordings";

static constexpr const char* KEY_NEXT_ID = "next_rec_id";

struct Entry {
    uint32_t     id;
    irec::Header header;
    uint64_t     size_bytes;
};

// Sorted by ID. Large indexes land in PSRAM via malloc; it's never touched
// during capture, since everything that uses it is refused while recording.
static std::vector<Entry>    index_;
static SemaphoreHandle_t     index_lock = nullptr;
static std::atomic<uint32_t> count{0};
static uint32_t              next_id = 1;

class IndexLock {
public:
    IndexLock()  { xSemaphoreTake(index_lock, portMAX_DELAY); }
    ~IndexLock() { xSemaphoreGive(index_lock); }
};

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

// Reads one file's header from flash — only at boot
static bool read_entry(uint32_t id, Entry* out) {
    char path[48];
    recording::path_for_id(id, path, sizeof(path));
    struct stat st;
    FILE* f = (stat(path, &st) == 0) ? fopen(path, "rb") : nullptr;
    if (!f) return false;

    uint8_t raw[irec::HEADER_SIZE];
    bool valid = fread(raw, 1, sizeof(raw), f) == sizeof(raw) && irec::decode_header(raw, &out->header);
    fclose(f);
    if (!valid) {
        ESP_LOGW(TAG, "Skipping %s: bad header", path);
        return false;
    }
    out->id         = id;
    out->size_bytes = st.st_size;
    return true;
}

void recordings_init() {
    index_lock = xSemaphoreCreateMutex();
    configASSERT(index_lock);

    if (mkdir(recording::DIR, 0775) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "Can't create %s: %s", recording::DIR, strerror(errno));
        return;
    }
    remove_temp_files();

    std::vector<uint32_t> ids;
    scan_ids(&ids);
    for (uint32_t id : ids) {
        Entry e;
        if (read_entry(id, &e)) index_.push_back(e);
    }
    count = index_.size();

    // Never hand out an ID that's on flash (even a bad file's), even if NVS was erased
    uint32_t stored = 1;
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u32(h, KEY_NEXT_ID, &stored);
        nvs_close(h);
    }
    uint32_t after_existing = ids.empty() ? 1 : ids.back() + 1;
    next_id = std::max(stored, after_existing);

    ESP_LOGI(TAG, "%u recordings, next ID %lu", (unsigned)index_.size(), (unsigned long)next_id);
}

uint32_t recordings_count() { return count.load(); }

void recordings_for_each(const RecordingVisitor& visit) {
    // Visit a copy: the visitor may be slow (e.g. sending each entry over HTTP)
    std::vector<Entry> snapshot;
    {
        IndexLock lock;
        snapshot = index_;
    }
    for (const Entry& e : snapshot) visit(e.id, e.header, e.size_bytes);
}

void recordings_put(uint32_t id, const irec::Header& h, uint64_t size_bytes) {
    IndexLock lock;
    auto it = std::lower_bound(index_.begin(), index_.end(), id,
                               [](const Entry& e, uint32_t v) { return e.id < v; });
    if (it != index_.end() && it->id == id) {
        it->header     = h;
        it->size_bytes = size_bytes;
    } else {
        index_.insert(it, {id, h, size_bytes});
    }
    count = index_.size();
}

static void index_remove(uint32_t id) {
    IndexLock lock;
    auto it = std::lower_bound(index_.begin(), index_.end(), id,
                               [](const Entry& e, uint32_t v) { return e.id < v; });
    if (it != index_.end() && it->id == id) index_.erase(it);
    count = index_.size();
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
    index_remove(id);
    storage_refresh();
    ESP_LOGI(TAG, "Deleted %s", path);
    return ESP_OK;
}

uint32_t recordings_delete_all() {
    // From the directory, not the index, so files with a bad header go too
    std::vector<uint32_t> ids;
    scan_ids(&ids);

    uint32_t deleted = 0;
    for (uint32_t id : ids) {
        char path[48];
        recording::path_for_id(id, path, sizeof(path));
        if (unlink(path) == 0) {
            deleted++;
            index_remove(id);
        }
        vTaskDelay(1);  // let lower-priority tasks and the idle task run
    }
    storage_refresh();
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
