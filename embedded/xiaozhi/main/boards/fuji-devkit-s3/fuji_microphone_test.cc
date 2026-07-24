#include "fuji_microphone_test.h"

#include "audio/audio_codec.h"
#include "config.h"
#include "display/display.h"

#include <driver/i2s_std.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

#define TAG "FujiMicTest"

namespace {

void SetDisplayStatus(Display* display, const char* status, const char* message) {
    if (display == nullptr) {
        return;
    }
    display->SetStatus(status);
    display->SetChatMessage("system", message);
}

bool CheckStep(esp_err_t result, const char* step, Display* display) {
    if (result == ESP_OK) {
        return true;
    }
    ESP_LOGE(TAG, "microphone %s failed: %s; speaker remains disabled",
             step, esp_err_to_name(result));
    SetDisplayStatus(display, "MIC ERROR", step);
    return false;
}

}  // namespace

bool RunFujiMicrophoneTest(Display* display) {
    ESP_LOGI(TAG,
             "microphone-only test: BCLK=%d WS=%d DIN=%d, DOUT unused, amplifier disabled",
             AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DIN);
    SetDisplayStatus(display, "MIC READY", "speak after 2 seconds");
    vTaskDelay(pdMS_TO_TICKS(2000));

    i2s_chan_handle_t rx_handle = nullptr;
    i2s_chan_config_t channel_config = {
        .id = XIAOZHI_I2S_PORT(0),
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM,
        .dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    if (!CheckStep(
            i2s_new_channel(&channel_config, nullptr, &rx_handle),
            "I2S RX channel setup",
            display)) {
        return false;
    }

    i2s_std_config_t i2s_config = {
        .clk_cfg = {
            .sample_rate_hz = AUDIO_INPUT_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
#ifdef I2S_HW_VERSION_2
            .ext_clk_freq_hz = 0,
#endif
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_LEFT,
            .ws_width = I2S_DATA_BIT_WIDTH_32BIT,
            .ws_pol = false,
            .bit_shift = true,
#ifdef I2S_HW_VERSION_2
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false,
#endif
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = AUDIO_I2S_GPIO_BCLK,
            .ws = AUDIO_I2S_GPIO_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = AUDIO_I2S_GPIO_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    if (!CheckStep(
            i2s_channel_init_std_mode(rx_handle, &i2s_config),
            "I2S standard mode setup",
            display)) {
        i2s_del_channel(rx_handle);
        return false;
    }
    if (!CheckStep(i2s_channel_enable(rx_handle), "I2S RX enable", display)) {
        i2s_del_channel(rx_handle);
        return false;
    }

    SetDisplayStatus(display, "MIC ACTIVE", "speak or clap now");
    ESP_LOGI(TAG, "microphone capture started: speak or clap for 8 seconds");
    vTaskDelay(pdMS_TO_TICKS(120));

    constexpr int kFrameSamples = AUDIO_INPUT_SAMPLE_RATE / 50;
    constexpr int kFramesPerWindow = 10;
    constexpr int kWindowCount = 40;
    std::vector<int32_t> raw_samples(kFrameSamples);
    int observed_min = INT16_MAX;
    int observed_max = INT16_MIN;
    int total_peak = 0;
    int64_t total_energy = 0;
    size_t total_samples = 0;

    for (int window = 0; window < kWindowCount; ++window) {
        int window_peak = 0;
        int64_t window_energy = 0;
        size_t window_samples = 0;
        int read_failures = 0;
        for (int frame = 0; frame < kFramesPerWindow; ++frame) {
            size_t bytes_read = 0;
            const esp_err_t read_result = i2s_channel_read(
                rx_handle,
                raw_samples.data(),
                raw_samples.size() * sizeof(int32_t),
                &bytes_read,
                300);
            if (read_result != ESP_OK) {
                ++read_failures;
                continue;
            }

            const size_t sample_count = bytes_read / sizeof(int32_t);
            for (size_t i = 0; i < sample_count; ++i) {
                const int32_t shifted = raw_samples[i] >> 12;
                const int sample = std::clamp(
                    shifted,
                    static_cast<int32_t>(INT16_MIN),
                    static_cast<int32_t>(INT16_MAX));
                observed_min = std::min(observed_min, sample);
                observed_max = std::max(observed_max, sample);
                const int magnitude = std::abs(sample);
                window_peak = std::max(window_peak, magnitude);
                window_energy += static_cast<int64_t>(sample) * sample;
            }
            window_samples += sample_count;
        }

        if (window_samples == 0) {
            ESP_LOGW(TAG, "microphone window=%d/%d has no samples (%d read failures)",
                     window + 1, kWindowCount, read_failures);
            continue;
        }

        const double window_rms = std::sqrt(
            static_cast<double>(window_energy) / static_cast<double>(window_samples));
        ESP_LOGI(TAG, "microphone window=%d/%d samples=%zu peak=%d rms=%.1f failures=%d",
                 window + 1, kWindowCount, window_samples, window_peak,
                 window_rms, read_failures);
        char level_text[32];
        std::snprintf(level_text, sizeof(level_text), "P:%d R:%.0f", window_peak, window_rms);
        SetDisplayStatus(display, "MIC ACTIVE", level_text);

        total_peak = std::max(total_peak, window_peak);
        total_energy += window_energy;
        total_samples += window_samples;
    }

    CheckStep(i2s_channel_disable(rx_handle), "I2S RX disable", display);
    CheckStep(i2s_del_channel(rx_handle), "I2S RX cleanup", display);

    if (total_samples == 0) {
        ESP_LOGE(TAG, "microphone test failed: no I2S samples were captured");
        SetDisplayStatus(display, "MIC ERROR", "no I2S samples");
        return false;
    }

    const double total_rms = std::sqrt(
        static_cast<double>(total_energy) / static_cast<double>(total_samples));
    ESP_LOGI(TAG, "microphone summary samples=%zu min=%d max=%d peak=%d rms=%.1f",
             total_samples, observed_min, observed_max, total_peak, total_rms);
    char summary[32];
    std::snprintf(summary, sizeof(summary), "P:%d R:%.1f", total_peak, total_rms);
    if (total_rms < 2.0) {
        ESP_LOGW(TAG, "microphone signal is effectively silent; check VDD/GND, L/R and SD wiring");
        SetDisplayStatus(display, "MIC SILENT", summary);
    } else {
        SetDisplayStatus(display, "MIC DONE", summary);
    }
    return true;
}
