#include "fuji_microphone_test.h"

#include "audio/audio_codec.h"
#include "config.h"
#include "display/display.h"

#include <driver/gpio.h>
#include <driver/i2s_std.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <inttypes.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

#define TAG "FujiMicTest"

namespace {

constexpr int kFrameSamples = AUDIO_INPUT_SAMPLE_RATE / 50;
constexpr int kFramesPerWindow = 10;
constexpr int kWindowsPerSecond = 5;

struct CaptureStats {
    int observed_min = INT16_MAX;
    int observed_max = INT16_MIN;
    int peak = 0;
    int64_t energy = 0;
    int64_t sum = 0;
    size_t samples = 0;
    size_t xiaozhi_clipped_samples = 0;
    size_t nonzero_padding_samples = 0;
    int read_failures = 0;

    double Rms() const {
        return samples == 0
            ? 0.0
            : std::sqrt(static_cast<double>(energy) / static_cast<double>(samples));
    }

    double DcOffset() const {
        return samples == 0
            ? 0.0
            : static_cast<double>(sum) / static_cast<double>(samples);
    }

    double XiaozhiClipPercent() const {
        return samples == 0
            ? 0.0
            : 100.0 * static_cast<double>(xiaozhi_clipped_samples) /
                  static_cast<double>(samples);
    }

    double NonzeroPaddingPercent() const {
        return samples == 0
            ? 0.0
            : 100.0 * static_cast<double>(nonzero_padding_samples) /
                  static_cast<double>(samples);
    }
};

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

bool WaitForCaptureStart(Display* display) {
    gpio_config_t button_config = {};
    button_config.pin_bit_mask = 1ULL << MIC_TEST_START_GPIO;
    button_config.mode = GPIO_MODE_INPUT;
    button_config.pull_up_en = GPIO_PULLUP_ENABLE;
    button_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    button_config.intr_type = GPIO_INTR_DISABLE;
    if (!CheckStep(gpio_config(&button_config), "BOOT button setup", display)) {
        return false;
    }

    SetDisplayStatus(display, "MIC RECORD", "press BOOT to start");
    ESP_LOGI(TAG, "WAV capture armed; press and release BOOT (GPIO%d) to start",
             MIC_TEST_START_GPIO);
    while (gpio_get_level(MIC_TEST_START_GPIO) != 0) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    while (gpio_get_level(MIC_TEST_START_GPIO) == 0) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    ESP_LOGI(TAG, "BOOT released; controlled WAV capture will start in 3 seconds");
    SetDisplayStatus(display, "MIC READY", "keep quiet in 3 sec");
    vTaskDelay(pdMS_TO_TICKS(3000));
    return true;
}

void WriteLe16(uint8_t* destination, uint16_t value) {
    destination[0] = static_cast<uint8_t>(value & 0xffU);
    destination[1] = static_cast<uint8_t>((value >> 8) & 0xffU);
}

void WriteLe32(uint8_t* destination, uint32_t value) {
    destination[0] = static_cast<uint8_t>(value & 0xffU);
    destination[1] = static_cast<uint8_t>((value >> 8) & 0xffU);
    destination[2] = static_cast<uint8_t>((value >> 16) & 0xffU);
    destination[3] = static_cast<uint8_t>((value >> 24) & 0xffU);
}

bool SaveWavCapture(const std::vector<int16_t>& recording, Display* display) {
    constexpr size_t kWavHeaderSize = 44;
    constexpr size_t kFlashSectorSize = 4096;
    const size_t pcm_bytes = recording.size() * sizeof(int16_t);
    const size_t wav_bytes = kWavHeaderSize + pcm_bytes;
    if (recording.empty() || wav_bytes > UINT32_MAX) {
        ESP_LOGE(TAG, "WAV capture has an invalid size: samples=%zu", recording.size());
        SetDisplayStatus(display, "WAV ERROR", "invalid capture size");
        return false;
    }

    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_ANY,
        "mic_capture");
    if (partition == nullptr) {
        ESP_LOGE(TAG, "mic_capture partition was not found");
        SetDisplayStatus(display, "WAV ERROR", "partition missing");
        return false;
    }
    if (wav_bytes > partition->size) {
        ESP_LOGE(TAG, "WAV needs %zu bytes but mic_capture has %zu bytes",
                 wav_bytes, partition->size);
        SetDisplayStatus(display, "WAV ERROR", "partition too small");
        return false;
    }

    std::array<uint8_t, kWavHeaderSize> header = {};
    std::memcpy(header.data(), "RIFF", 4);
    WriteLe32(header.data() + 4, static_cast<uint32_t>(wav_bytes - 8));
    std::memcpy(header.data() + 8, "WAVEfmt ", 8);
    WriteLe32(header.data() + 16, 16);
    WriteLe16(header.data() + 20, 1);
    WriteLe16(header.data() + 22, 1);
    WriteLe32(header.data() + 24, AUDIO_INPUT_SAMPLE_RATE);
    WriteLe32(header.data() + 28, AUDIO_INPUT_SAMPLE_RATE * sizeof(int16_t));
    WriteLe16(header.data() + 32, sizeof(int16_t));
    WriteLe16(header.data() + 34, 16);
    std::memcpy(header.data() + 36, "data", 4);
    WriteLe32(header.data() + 40, static_cast<uint32_t>(pcm_bytes));

    const size_t erase_bytes =
        (wav_bytes + kFlashSectorSize - 1) & ~(kFlashSectorSize - 1);
    if (!CheckStep(
            esp_partition_erase_range(partition, 0, erase_bytes),
            "WAV partition erase",
            display) ||
        !CheckStep(
            esp_partition_write(partition, 0, header.data(), header.size()),
            "WAV header write",
            display) ||
        !CheckStep(
            esp_partition_write(
                partition,
                header.size(),
                recording.data(),
                pcm_bytes),
            "WAV PCM write",
            display)) {
        return false;
    }

    ESP_LOGI(TAG,
             "WAV ready: partition=mic_capture flash_offset=0x%08" PRIx32 " bytes=%zu samples=%zu format=PCM_S16LE_1CH_%dHz",
             partition->address, wav_bytes, recording.size(), AUDIO_INPUT_SAMPLE_RATE);
    SetDisplayStatus(display, "WAV READY", "capture saved");
    return true;
}

bool CapturePhase(i2s_chan_handle_t rx_handle,
                  Display* display,
                  const char* phase,
                  const char* instruction,
                  int duration_seconds,
                  CaptureStats& stats,
                  std::vector<int16_t>& recording) {
    SetDisplayStatus(display, phase, instruction);
    ESP_LOGI(TAG, "phase=%s started for %d seconds: %s",
             phase, duration_seconds, instruction);

    std::vector<int32_t> raw_samples(kFrameSamples);
    const int window_count = duration_seconds * kWindowsPerSecond;
    for (int window = 0; window < window_count; ++window) {
        int window_peak = 0;
        int64_t window_energy = 0;
        size_t window_samples = 0;
        size_t window_xiaozhi_clipped = 0;
        int window_read_failures = 0;

        for (int frame = 0; frame < kFramesPerWindow; ++frame) {
            size_t bytes_read = 0;
            const esp_err_t read_result = i2s_channel_read(
                rx_handle,
                raw_samples.data(),
                raw_samples.size() * sizeof(int32_t),
                &bytes_read,
                300);
            if (read_result != ESP_OK) {
                ++window_read_failures;
                continue;
            }

            const size_t sample_count = bytes_read / sizeof(int32_t);
            for (size_t i = 0; i < sample_count; ++i) {
                const int32_t raw_sample = raw_samples[i];

                // A 24-bit I2S microphone is left-aligned in the 32-bit slot.
                // Shifting by 16 gives an unamplified 16-bit diagnostic value.
                const int native_sample = raw_sample >> 16;
                stats.observed_min = std::min(stats.observed_min, native_sample);
                stats.observed_max = std::max(stats.observed_max, native_sample);
                const int magnitude = std::abs(native_sample);
                window_peak = std::max(window_peak, magnitude);
                window_energy += static_cast<int64_t>(native_sample) * native_sample;
                stats.sum += native_sample;
                recording.push_back(static_cast<int16_t>(native_sample));

                // Upstream NoAudioCodec shifts by 12, which adds 24 dB relative
                // to the native conversion. Count its potential clipping without
                // changing the production path during hardware bring-up.
                const int32_t xiaozhi_sample = raw_sample >> 12;
                if (xiaozhi_sample > INT16_MAX || xiaozhi_sample < INT16_MIN) {
                    ++window_xiaozhi_clipped;
                }
                if ((static_cast<uint32_t>(raw_sample) & 0xffU) != 0) {
                    ++stats.nonzero_padding_samples;
                }
            }
            window_samples += sample_count;
        }

        if (window_samples == 0) {
            ESP_LOGW(TAG, "phase=%s window=%d/%d has no samples (%d read failures)",
                     phase, window + 1, window_count, window_read_failures);
            stats.read_failures += window_read_failures;
            continue;
        }

        const double window_rms = std::sqrt(
            static_cast<double>(window_energy) / static_cast<double>(window_samples));
        const double window_clip_percent =
            100.0 * static_cast<double>(window_xiaozhi_clipped) /
            static_cast<double>(window_samples);
        ESP_LOGI(TAG,
                 "phase=%s window=%d/%d samples=%zu native_peak=%d native_rms=%.1f xiaozhi_clipped=%zu (%.3f%%) failures=%d",
                 phase, window + 1, window_count, window_samples, window_peak,
                 window_rms, window_xiaozhi_clipped, window_clip_percent,
                 window_read_failures);

        char level_text[32];
        std::snprintf(level_text, sizeof(level_text), "P:%d R:%.0f", window_peak, window_rms);
        SetDisplayStatus(display, phase, level_text);

        stats.peak = std::max(stats.peak, window_peak);
        stats.energy += window_energy;
        stats.samples += window_samples;
        stats.xiaozhi_clipped_samples += window_xiaozhi_clipped;
        stats.read_failures += window_read_failures;
    }

    if (stats.samples == 0) {
        ESP_LOGE(TAG, "phase=%s failed: no I2S samples were captured", phase);
        return false;
    }

    ESP_LOGI(TAG,
             "phase=%s summary samples=%zu native_min=%d native_max=%d native_peak=%d native_rms=%.1f native_dc=%.1f xiaozhi_clipped=%zu (%.3f%%) padding_nonzero=%zu (%.3f%%) failures=%d",
             phase, stats.samples, stats.observed_min, stats.observed_max,
             stats.peak, stats.Rms(), stats.DcOffset(),
             stats.xiaozhi_clipped_samples, stats.XiaozhiClipPercent(),
             stats.nonzero_padding_samples, stats.NonzeroPaddingPercent(),
             stats.read_failures);
    return true;
}

}  // namespace

bool RunFujiMicrophoneTest(Display* display) {
    ESP_LOGI(TAG,
             "microphone-only test: BCLK=%d WS=%d DIN=%d, DOUT unused, amplifier disabled",
             AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DIN);
    if (!WaitForCaptureStart(display)) {
        return false;
    }

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

    ESP_LOGI(TAG, "controlled microphone capture started: QUIET, SPEAK, then CLAP");
    vTaskDelay(pdMS_TO_TICKS(120));

    CaptureStats quiet;
    CaptureStats speech;
    CaptureStats clap;
    std::vector<int16_t> recording;
    recording.reserve(AUDIO_INPUT_SAMPLE_RATE * 11);
    const bool quiet_ok = CapturePhase(
        rx_handle, display, "MIC QUIET", "do not speak", 3, quiet, recording);
    const bool speech_ok = CapturePhase(
        rx_handle, display, "MIC SPEAK", "speak normally", 5, speech, recording);
    const bool clap_ok = CapturePhase(
        rx_handle, display, "MIC CLAP", "clap at arm length", 3, clap, recording);

    CheckStep(i2s_channel_disable(rx_handle), "I2S RX disable", display);
    CheckStep(i2s_del_channel(rx_handle), "I2S RX cleanup", display);

    if (!quiet_ok || !speech_ok || !clap_ok) {
        ESP_LOGE(TAG, "microphone test failed: one or more capture phases had no samples");
        SetDisplayStatus(display, "MIC ERROR", "no I2S samples");
        return false;
    }

    if (!SaveWavCapture(recording, display)) {
        return false;
    }

    const double quiet_rms = quiet.Rms();
    const double speech_rms = speech.Rms();
    const double clap_rms = clap.Rms();
    const double action_rms = std::max(speech_rms, clap_rms);
    const double response_ratio = action_rms / std::max(quiet_rms, 1.0);
    ESP_LOGI(TAG,
             "controlled summary native_rms quiet=%.1f speech=%.1f clap=%.1f response_ratio=%.2f",
             quiet_rms, speech_rms, clap_rms, response_ratio);

    char summary[32];
    std::snprintf(summary, sizeof(summary), "Q:%.0f V:%.0f C:%.0f",
                  quiet_rms, speech_rms, clap_rms);
    if (action_rms < 2.0) {
        ESP_LOGW(TAG, "microphone signal is effectively silent; check VDD/GND, L/R and SD wiring");
        SetDisplayStatus(display, "MIC SILENT", summary);
    } else {
        if (response_ratio < 1.5) {
            ESP_LOGW(TAG,
                     "controlled sound response is weak; repeat the test while following the OLED phases");
        }
        const size_t total_samples = quiet.samples + speech.samples + clap.samples;
        const size_t total_xiaozhi_clipped =
            quiet.xiaozhi_clipped_samples + speech.xiaozhi_clipped_samples +
            clap.xiaozhi_clipped_samples;
        const double total_clip_percent =
            100.0 * static_cast<double>(total_xiaozhi_clipped) /
            static_cast<double>(total_samples);
        if (total_clip_percent > 1.0) {
            ESP_LOGW(TAG,
                     "upstream >>12 scaling would clip %.3f%% of samples; native metrics remain valid",
                     total_clip_percent);
        }
        ESP_LOGI(TAG, "controlled signal summary for OLED: %s", summary);
    }
    return true;
}
