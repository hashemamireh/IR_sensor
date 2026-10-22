#include "storage.h"

#include "esp_log.h"
#include "esp_littlefs.h"

#include "config.h"

static const char* TAG = "storage";

void storage_init() {
    esp_vfs_littlefs_conf_t conf = {
        .base_path       = "/data",
        .partition_label = LITTLEFS_PARTITION_LABEL,
        .format_if_mount_failed = true,
        .dont_mount      = false,
    };
    ESP_ERROR_CHECK(esp_vfs_littlefs_register(&conf));

    size_t total = 0, used = 0;
    storage_info(&total, &used);
    ESP_LOGI(TAG, "LittleFS mounted: %zu KB used / %zu KB total", used / 1024, total / 1024);
}

void storage_info(size_t* total_bytes, size_t* used_bytes) {
    *total_bytes = 0;
    *used_bytes  = 0;
    esp_littlefs_info(LITTLEFS_PARTITION_LABEL, total_bytes, used_bytes);
}
