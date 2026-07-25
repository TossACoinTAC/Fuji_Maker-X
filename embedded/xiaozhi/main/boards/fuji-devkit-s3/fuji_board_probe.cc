#include "board_probe.h"

#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>

#include <cinttypes>

#define TAG "FujiBoardProbe"

namespace {

const char* ResetReasonName(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON: return "power-on";
        case ESP_RST_EXT: return "external-pin";
        case ESP_RST_SW: return "software";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT: return "interrupt-watchdog";
        case ESP_RST_TASK_WDT: return "task-watchdog";
        case ESP_RST_WDT: return "watchdog";
        case ESP_RST_DEEPSLEEP: return "deep-sleep-wakeup";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_SDIO: return "sdio";
        case ESP_RST_USB: return "usb";
        case ESP_RST_JTAG: return "jtag";
        case ESP_RST_EFUSE: return "efuse-error";
        case ESP_RST_PWR_GLITCH: return "power-glitch";
        case ESP_RST_CPU_LOCKUP: return "cpu-lockup";
        case ESP_RST_UNKNOWN:
        default: return "unknown";
    }
}

}  // namespace

void RunBoardProbe() {
    esp_chip_info_t chip_info = {};
    esp_chip_info(&chip_info);

    uint32_t flash_size = 0;
    const esp_err_t flash_result = esp_flash_get_size(nullptr, &flash_size);
    const size_t psram_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);

    ESP_LOGI(TAG, "board=fuji-devkit-s3 idf=%s", esp_get_idf_version());
    ESP_LOGI(TAG, "chip=ESP32-S3 model_id=%d cores=%d revision=%d features=0x%" PRIx32,
             chip_info.model, chip_info.cores, chip_info.revision, chip_info.features);
    if (flash_result == ESP_OK) {
        ESP_LOGI(TAG, "flash=%" PRIu32 " bytes (%" PRIu32 " MiB)",
                 flash_size, flash_size / (1024 * 1024));
    } else {
        ESP_LOGE(TAG, "flash size query failed: %s", esp_err_to_name(flash_result));
    }
    ESP_LOGI(TAG, "psram=%zu bytes (%zu MiB)", psram_size, psram_size / (1024 * 1024));
    const esp_reset_reason_t reset_reason = esp_reset_reason();
    ESP_LOGI(TAG, "reset_reason=%s (%d)",
             ResetReasonName(reset_reason), static_cast<int>(reset_reason));

    if (flash_size != 16 * 1024 * 1024) {
        ESP_LOGW(TAG, "expected 16 MiB flash for N16R8; verify the module marking and build config");
    }
    if (psram_size != 8 * 1024 * 1024) {
        ESP_LOGW(TAG, "expected 8 MiB PSRAM for N16R8; verify Octal PSRAM configuration");
    }
}
