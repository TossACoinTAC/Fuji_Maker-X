#include "waveshare_microphone_test.h"

#include "audio/audio_codec.h"
#include "config.h"
#include "display/display.h"

#include <driver/gpio.h>
#include <driver/i2s_std.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#define TAG "FujiWsMicTest"

namespace {

constexpr int kFrameSamples = AUDIO_INPUT_SAMPLE_RATE / 50;
constexpr uint32_t kReadTimeoutMs = 200;
constexpr size_t kWavHeaderSize = 44;
constexpr size_t kCaptureWavBytes = kWavHeaderSize + AUDIO_INPUT_SAMPLE_RATE * 11 * sizeof(int16_t);
bool capture_partition_prepared = false;

struct CaptureStats {
    int peak = 0;
    int64_t energy = 0;
    size_t samples = 0;
    size_t upstream_clipped = 0;
    int read_failures = 0;

    double Rms() const {
        return samples == 0 ? 0.0
                            : std::sqrt(static_cast<double>(energy) / static_cast<double>(samples));
    }
};

void Show(Display* display, const char* status, const char* message) {
    if (display != nullptr) {
        display->SetStatus(status);
        display->SetChatMessage("system", message);
    }
}

bool Check(esp_err_t result, const char* step, Display* display) {
    if (result == ESP_OK) {
        return true;
    }
    ESP_LOGE(TAG, "%s failed: %s; speaker remains disabled", step, esp_err_to_name(result));
    Show(display, "MIC ERROR", step);
    return false;
}

bool WaitForBoot(Display* display) {
    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << BOOT_BUTTON_GPIO;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    if (!Check(gpio_config(&config), "BOOT setup", display)) {
        return false;
    }
    Show(display, "MIC ARMED", "press BOOT to start");
    ESP_LOGI(TAG, "capture armed; press and release BOOT, then follow QUIET/SPEAK/CLAP");
    while (gpio_get_level(BOOT_BUTTON_GPIO) != 0) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    while (gpio_get_level(BOOT_BUTTON_GPIO) == 0) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    Show(display, "MIC READY", "quiet in 3 seconds");
    vTaskDelay(pdMS_TO_TICKS(3000));
    return true;
}

void WriteLe16(uint8_t* output, uint16_t value) {
    output[0] = value & 0xffU;
    output[1] = (value >> 8) & 0xffU;
}

void WriteLe32(uint8_t* output, uint32_t value) {
    output[0] = value & 0xffU;
    output[1] = (value >> 8) & 0xffU;
    output[2] = (value >> 16) & 0xffU;
    output[3] = (value >> 24) & 0xffU;
}

bool EraseWavRange(const esp_partition_t* partition, size_t bytes, Display* display) {
    constexpr size_t kSectorSize = 4096;
    for (size_t offset = 0; offset < bytes; offset += kSectorSize) {
        if (!Check(esp_partition_erase_range(partition, offset, kSectorSize), "WAV erase",
                   display)) {
            return false;
        }
        vTaskDelay(1);
    }
    return true;
}

bool WriteWavData(const esp_partition_t* partition, size_t offset, const void* data, size_t bytes,
                  Display* display) {
    constexpr size_t kWriteChunkSize = 4096;
    const auto* source = static_cast<const uint8_t*>(data);
    for (size_t written = 0; written < bytes; written += kWriteChunkSize) {
        const size_t chunk_size = std::min(kWriteChunkSize, bytes - written);
        if (!Check(esp_partition_write(partition, offset + written, source + written, chunk_size),
                   "WAV write", display)) {
            return false;
        }
        vTaskDelay(1);
    }
    return true;
}

bool SaveWav(const std::vector<int16_t>& recording, Display* display) {
    const size_t pcm_bytes = recording.size() * sizeof(int16_t);
    const size_t wav_bytes = kWavHeaderSize + pcm_bytes;
    const esp_partition_t* partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "mic_capture");
    if (partition == nullptr || recording.empty() || wav_bytes > partition->size ||
        !capture_partition_prepared) {
        ESP_LOGE(TAG, "mic_capture is missing or too small: WAV=%zu", wav_bytes);
        Show(display, "WAV ERROR", "partition unavailable");
        return false;
    }

    std::array<uint8_t, kWavHeaderSize> header = {};
    std::memcpy(header.data(), "RIFF", 4);
    WriteLe32(header.data() + 4, wav_bytes - 8);
    std::memcpy(header.data() + 8, "WAVEfmt ", 8);
    WriteLe32(header.data() + 16, 16);
    WriteLe16(header.data() + 20, 1);
    WriteLe16(header.data() + 22, 1);
    WriteLe32(header.data() + 24, AUDIO_INPUT_SAMPLE_RATE);
    WriteLe32(header.data() + 28, AUDIO_INPUT_SAMPLE_RATE * sizeof(int16_t));
    WriteLe16(header.data() + 32, sizeof(int16_t));
    WriteLe16(header.data() + 34, 16);
    std::memcpy(header.data() + 36, "data", 4);
    WriteLe32(header.data() + 40, pcm_bytes);

    if (!WriteWavData(partition, 0, header.data(), header.size(), display) ||
        !WriteWavData(partition, header.size(), recording.data(), pcm_bytes, display)) {
        return false;
    }
    ESP_LOGI(TAG,
             "WAV ready: partition=mic_capture offset=0x%08lx bytes=%zu format=PCM_S16LE_1CH_%dHz",
             static_cast<unsigned long>(partition->address), wav_bytes, AUDIO_INPUT_SAMPLE_RATE);
    Show(display, "WAV READY", "capture saved");
    return true;
}

bool CapturePhase(i2s_chan_handle_t rx, Display* display, const char* phase,
                  const char* instruction, int seconds, CaptureStats& stats,
                  std::vector<int16_t>& recording) {
    Show(display, phase, instruction);
    ESP_LOGI(TAG, "phase=%s duration=%d: %s", phase, seconds, instruction);
    std::array<int32_t, kFrameSamples> raw = {};
    const int frames = seconds * 50;
    for (int frame = 0; frame < frames; ++frame) {
        size_t bytes_read = 0;
        const esp_err_t result =
            i2s_channel_read(rx, raw.data(), sizeof(raw), &bytes_read, kReadTimeoutMs);
        if (result != ESP_OK) {
            if (stats.read_failures == 0) {
                ESP_LOGE(TAG, "phase=%s first I2S read failed: %s", phase, esp_err_to_name(result));
            }
            ++stats.read_failures;
            continue;
        }
        const size_t count = bytes_read / sizeof(int32_t);
        for (size_t index = 0; index < count; ++index) {
            const int native = raw[index] >> 16;
            const int upstream = raw[index] >> 12;
            stats.peak = std::max(stats.peak, std::abs(native));
            stats.energy += static_cast<int64_t>(native) * native;
            stats.upstream_clipped += upstream > INT16_MAX || upstream < INT16_MIN;
            recording.push_back(static_cast<int16_t>(native));
        }
        stats.samples += count;
        if ((frame + 1) % 10 == 0) {
            ESP_LOGI(TAG, "phase=%s elapsed=%.1fs samples=%zu peak=%d rms=%.1f failures=%d", phase,
                     static_cast<double>(frame + 1) / 50.0, stats.samples, stats.peak, stats.Rms(),
                     stats.read_failures);
        }
    }
    const double clip_percent =
        stats.samples == 0 ? 0.0 : 100.0 * stats.upstream_clipped / stats.samples;
    ESP_LOGI(TAG, "phase=%s summary samples=%zu peak=%d rms=%.1f clipped=%.3f%% failures=%d", phase,
             stats.samples, stats.peak, stats.Rms(), clip_percent, stats.read_failures);
    return stats.samples != 0;
}

}  // namespace

bool PrepareWaveshareMicrophoneCapture() {
    constexpr size_t kSectorSize = 4096;
    const esp_partition_t* partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "mic_capture");
    if (partition == nullptr || kCaptureWavBytes > partition->size) {
        ESP_LOGE(TAG, "mic_capture is missing or too small for %zu bytes", kCaptureWavBytes);
        return false;
    }
    const size_t erase_bytes = (kCaptureWavBytes + kSectorSize - 1) & ~(kSectorSize - 1);
    ESP_LOGI(TAG, "pre-erasing %zu bytes before display/LVGL initialization", erase_bytes);
    capture_partition_prepared = EraseWavRange(partition, erase_bytes, nullptr);
    return capture_partition_prepared;
}

bool RunWaveshareMicrophoneTest(Display* display) {
    ESP_LOGI(TAG, "RX-only test: port=1 BCLK=%d WS=%d DIN=%d slot=right; speaker DOUT unused",
             AUDIO_I2S_MIC_GPIO_BCLK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
    if (!Check(gpio_intr_disable(TOUCH_INTERRUPT_GPIO), "touch interrupt disable", display) ||
        !WaitForBoot(display)) {
        return false;
    }
    ESP_LOGI(TAG, "touch interrupt disabled for deterministic Flash capture writes");

    i2s_chan_handle_t rx = nullptr;
    i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(XIAOZHI_I2S_PORT(1), I2S_ROLE_MASTER);
    if (!Check(i2s_new_channel(&channel_config, nullptr, &rx), "I2S RX create", display)) {
        return false;
    }
    i2s_std_config_t config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_INPUT_SAMPLE_RATE),
        .slot_cfg =
            I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg =
            {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = AUDIO_I2S_MIC_GPIO_BCLK,
                .ws = AUDIO_I2S_MIC_GPIO_WS,
                .dout = I2S_GPIO_UNUSED,
                .din = AUDIO_I2S_MIC_GPIO_DIN,
                .invert_flags = {},
            },
    };
    config.slot_cfg.slot_mask = AUDIO_I2S_MIC_SLOT;
    if (!Check(i2s_channel_init_std_mode(rx, &config), "I2S RX init", display) ||
        !Check(i2s_channel_enable(rx), "I2S RX enable", display)) {
        i2s_del_channel(rx);
        return false;
    }

    std::vector<int16_t> recording;
    recording.reserve(AUDIO_INPUT_SAMPLE_RATE * 11);
    CaptureStats quiet;
    CaptureStats speech;
    CaptureStats clap;
    const bool captured =
        CapturePhase(rx, display, "MIC QUIET", "remain quiet", 3, quiet, recording) &&
        CapturePhase(rx, display, "MIC SPEAK", "speak normally", 5, speech, recording) &&
        CapturePhase(rx, display, "MIC CLAP", "clap once", 3, clap, recording);
    Check(i2s_channel_disable(rx), "I2S RX disable", display);
    Check(i2s_del_channel(rx), "I2S RX delete", display);
    if (!captured || !SaveWav(recording, display)) {
        return false;
    }

    const double action_rms = std::max(speech.Rms(), clap.Rms());
    const double response_ratio = action_rms / std::max(quiet.Rms(), 1.0);
    ESP_LOGI(TAG, "controlled summary quiet=%.1f speech=%.1f clap=%.1f response_ratio=%.2f",
             quiet.Rms(), speech.Rms(), clap.Rms(), response_ratio);
    if (response_ratio < 1.5) {
        ESP_LOGW(TAG, "weak sound response; inspect the exported WAV before accepting microphone");
        Show(display, "MIC REVIEW", "weak response; export WAV");
    } else {
        Show(display, "MIC SAVED", "export and listen to WAV");
    }
    return true;
}
