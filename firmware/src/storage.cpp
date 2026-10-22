#include "storage.h"

#include <atomic>

#include "esp_log.h"
#include "esp_littlefs.h"

#include "config.h"

static const char* TAG = "storage";

static std::atomic<size_t> total_cached{0};
static std::atomic<size_t> used_cached{0};

void storage_init() {
    esp_vfs_littlefs_conf_t conf = {
        .base_path       = "/data",
        .partition_label = LITTLEFS_PARTITION_LABEL,
        .format_if_mount_failed = true,
        .dont_mount      = false,
    };
    ESP_ERROR_CHECK(esp_vfs_littlefs_register(&conf));

    storage_refresh();
    ESP_LOGI(TAG, "LittleFS mounted: %zu KB used / %zu KB total",
             used_cached.load() / 1024, total_cached.load() / 1024);
}

void storage_info(size_t* total_bytes, size_t* used_bytes) {
    *total_bytes = total_cached.load();
    *used_bytes  = used_cached.load();
}

void storage_refresh() {
    size_t total = 0, used = 0;
    if (esp_littlefs_info(LITTLEFS_PARTITION_LABEL, &total, &used) == ESP_OK) {
        total_cached = total;
        used_cached  = used;
    }
}
