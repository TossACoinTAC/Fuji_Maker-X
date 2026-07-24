#include "fuji_audio_codec.h"

#include "config.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#define TAG "FujiAudio"

int FujiAudioCodec::Read(int16_t* dest, int samples) {
    if (!software_muted_.load()) {
        return NoAudioCodec::Read(dest, samples);
    }

    std::fill_n(dest, samples, 0);
    const int delay_ms = std::max(1, samples * 1000 / input_sample_rate_);
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    return samples;
}

void FujiAudioCodec::EnableOutput(bool enable) {
    if (!enable) {
        gpio_set_level(AUDIO_AMP_ENABLE_GPIO, 0);
    }
    NoAudioCodec::EnableOutput(enable);
    if (enable) {
        gpio_set_level(AUDIO_AMP_ENABLE_GPIO, 1);
    }
}

FujiAudioCodec::FujiAudioCodec()
    : NoAudioCodecDuplex(
          AUDIO_INPUT_SAMPLE_RATE,
          AUDIO_OUTPUT_SAMPLE_RATE,
          AUDIO_I2S_GPIO_BCLK,
          AUDIO_I2S_GPIO_WS,
          AUDIO_I2S_GPIO_DOUT,
          AUDIO_I2S_GPIO_DIN) {
}

bool FujiAudioCodec::ToggleSoftwareMute() {
    const bool muted = !software_muted_.load();
    software_muted_.store(muted);
    ESP_LOGI(TAG, "software microphone mute=%s", muted ? "true" : "false");
    return muted;
}

void FujiAudioCodec::RunSelfTest() {
    ESP_LOGI(TAG, "audio self-test: microphone level capture");
    Start();
    EnableInput(true);
    vTaskDelay(pdMS_TO_TICKS(120));

    int16_t observed_min = INT16_MAX;
    int16_t observed_max = INT16_MIN;
    int64_t energy = 0;
    size_t observed_samples = 0;
    std::vector<int16_t> input(AUDIO_INPUT_SAMPLE_RATE / 50);
    for (int frame = 0; frame < 15; ++frame) {
        if (!InputData(input)) {
            ESP_LOGW(TAG, "microphone frame %d timed out", frame);
            continue;
        }
        int frame_peak = 0;
        for (int16_t sample : input) {
            observed_min = std::min(observed_min, sample);
            observed_max = std::max(observed_max, sample);
            frame_peak = std::max(frame_peak, std::abs(static_cast<int>(sample)));
            energy += static_cast<int64_t>(sample) * sample;
        }
        observed_samples += input.size();
        ESP_LOGI(TAG, "microphone frame=%d peak=%d", frame, frame_peak);
    }
    EnableInput(false);

    if (observed_samples == 0) {
        ESP_LOGE(TAG, "microphone self-test failed: no I2S samples");
    } else {
        const double rms = std::sqrt(
            static_cast<double>(energy) / static_cast<double>(observed_samples));
        ESP_LOGI(TAG, "microphone summary samples=%zu min=%d max=%d rms=%.1f",
                 observed_samples, observed_min, observed_max, rms);
        if (rms < 2.0) {
            ESP_LOGW(TAG, "microphone signal is effectively silent; check VDD/GND, L/R and SD wiring");
        }
    }

    ESP_LOGI(TAG, "audio self-test: 440 Hz tone at %d%% volume", AUDIO_SELF_TEST_VOLUME);
    const int saved_volume = output_volume_;
    output_volume_ = AUDIO_SELF_TEST_VOLUME;
    EnableOutput(true);

    constexpr double kPi = 3.14159265358979323846;
    constexpr double kToneFrequency = 440.0;
    constexpr int kToneDurationMs = 700;
    constexpr int kChunkSamples = 240;
    std::vector<int16_t> tone(kChunkSamples);
    int sample_index = 0;
    const int total_samples = AUDIO_OUTPUT_SAMPLE_RATE * kToneDurationMs / 1000;
    while (sample_index < total_samples) {
        const int chunk_size = std::min(kChunkSamples, total_samples - sample_index);
        tone.resize(chunk_size);
        for (int i = 0; i < chunk_size; ++i) {
            const double phase = 2.0 * kPi * kToneFrequency *
                static_cast<double>(sample_index + i) / AUDIO_OUTPUT_SAMPLE_RATE;
            tone[i] = static_cast<int16_t>(std::sin(phase) * 12000.0);
        }
        OutputData(tone);
        sample_index += chunk_size;
    }
    EnableOutput(false);
    output_volume_ = saved_volume;
    ESP_LOGI(TAG, "audio self-test complete; amplifier disabled");
}

void InitializeFujiAmplifierSafeState() {
    gpio_config_t amp_config = {};
    amp_config.pin_bit_mask = 1ULL << AUDIO_AMP_ENABLE_GPIO;
    amp_config.mode = GPIO_MODE_OUTPUT;
    const esp_err_t result = gpio_config(&amp_config);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "amplifier enable GPIO setup failed: %s", esp_err_to_name(result));
        return;
    }
    gpio_set_level(AUDIO_AMP_ENABLE_GPIO, 0);
    ESP_LOGI(TAG, "amplifier held disabled until I2S playback starts");
}
