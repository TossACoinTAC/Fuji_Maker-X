#include "wifi_board.h"

#include "codecs/no_audio_codec.h"
#include "config.h"

#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_idf_version.h>
#include <esp_log.h>
#include <esp_system.h>

#include <cinttypes>

#define TAG "FujiDevKitS3"

namespace {

void LogBoardProbe() {
    esp_chip_info_t chip_info = {};
    esp_chip_info(&chip_info);

    uint32_t flash_size = 0;
    const esp_err_t flash_result = esp_flash_get_size(nullptr, &flash_size);
    const size_t psram_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);

    ESP_LOGI(TAG, "board=fuji-devkit-s3 idf=%s", esp_get_idf_version());
    ESP_LOGI(TAG, "chip model=%d cores=%d revision=%d features=0x%" PRIx32,
             chip_info.model, chip_info.cores, chip_info.revision, chip_info.features);
    if (flash_result == ESP_OK) {
        ESP_LOGI(TAG, "flash=%" PRIu32 " bytes (%" PRIu32 " MiB)",
                 flash_size, flash_size / (1024 * 1024));
    } else {
        ESP_LOGE(TAG, "flash size query failed: %s", esp_err_to_name(flash_result));
    }
    ESP_LOGI(TAG, "psram=%zu bytes (%zu MiB)", psram_size, psram_size / (1024 * 1024));
    ESP_LOGI(TAG, "reset_reason=%d", static_cast<int>(esp_reset_reason()));

    if (flash_size != 16 * 1024 * 1024) {
        ESP_LOGW(TAG, "expected 16 MiB flash for N16R8; verify the module marking and build config");
    }
    if (psram_size != 8 * 1024 * 1024) {
        ESP_LOGW(TAG, "expected 8 MiB PSRAM for N16R8; verify Octal PSRAM configuration");
    }
}

}  // namespace

class FujiDevKitS3 : public WifiBoard {
public:
    FujiDevKitS3() {
        LogBoardProbe();
    }

    AudioCodec* GetAudioCodec() override {
        static NoAudioCodecDuplex audio_codec(
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN);
        return &audio_codec;
    }
};

DECLARE_BOARD(FujiDevKitS3);
