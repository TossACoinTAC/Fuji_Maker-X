#include "waveshare_speaker_test.h"

#include "assets/lang_config.h"
#include "audio/audio_codec.h"
#include "audio/demuxer/ogg_demuxer.h"
#include "config.h"
#include "display/display.h"

#include <driver/gpio.h>
#include <driver/i2s_std.h>
#include <esp_log.h>
#include <decoder/impl/esp_opus_dec.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#define TAG "FujiWsSpkTest"

namespace {

constexpr int kSpeakerTestSampleRate = 16000;
constexpr int kOpusFrameDurationMs = 60;
constexpr int32_t kLowDigitalGain = 2048;
constexpr uint32_t kSpeakerTaskStackSize = 32 * 1024;

void LogStack(const char* stage) {
    ESP_LOGI(TAG, "%s; minimum remaining stack=%u bytes", stage,
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

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
    ESP_LOGE(TAG, "%s failed: %s", step, esp_err_to_name(result));
    Show(display, "SPK ERROR", step);
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
    Show(display, "SPK ARMED", "volume MIN; press BOOT");
    ESP_LOGW(TAG, "speaker is silent; set the hardware volume knob to minimum, then press BOOT");
    while (gpio_get_level(BOOT_BUTTON_GPIO) != 0) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    while (gpio_get_level(BOOT_BUTTON_GPIO) == 0) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return true;
}

bool WritePcm(i2s_chan_handle_t tx, const int16_t* pcm, size_t samples, Display* display) {
    std::vector<int32_t> output(samples);
    for (size_t index = 0; index < samples; ++index) {
        output[index] = static_cast<int32_t>(pcm[index]) * kLowDigitalGain;
    }
    size_t bytes_written = 0;
    return Check(i2s_channel_write(tx, output.data(), output.size() * sizeof(int32_t),
                                   &bytes_written, portMAX_DELAY),
                 "I2S write", display) &&
           bytes_written == output.size() * sizeof(int32_t);
}

bool WriteSilence(i2s_chan_handle_t tx, int milliseconds, Display* display) {
    constexpr size_t kChunkSamples = 160;
    const std::array<int16_t, kChunkSamples> silence = {};
    const int chunks = milliseconds * kSpeakerTestSampleRate / 1000 / kChunkSamples;
    for (int index = 0; index < chunks; ++index) {
        if (!WritePcm(tx, silence.data(), silence.size(), display)) {
            return false;
        }
    }
    return true;
}

bool PlayPromptTone(i2s_chan_handle_t tx, Display* display) {
    constexpr int kToneSamples = kSpeakerTestSampleRate * 3 / 10;
    constexpr double kTwoPi = 6.283185307179586;
    std::vector<int16_t> tone(kToneSamples);
    for (int index = 0; index < kToneSamples; ++index) {
        const double envelope =
            std::min(1.0, std::min(index / 320.0, (kToneSamples - index) / 320.0));
        tone[index] = static_cast<int16_t>(
            2200.0 * envelope * std::sin(kTwoPi * 660.0 * index / kSpeakerTestSampleRate));
    }
    return WritePcm(tx, tone.data(), tone.size(), display);
}

bool PlayWelcomeSpeech(i2s_chan_handle_t tx, Display* display) {
    void* decoder = nullptr;
    esp_opus_dec_cfg_t decoder_config = ESP_OPUS_DEC_CONFIG_DEFAULT();
    decoder_config.sample_rate = kSpeakerTestSampleRate;
    decoder_config.channel = ESP_AUDIO_MONO;
    decoder_config.frame_duration = ESP_OPUS_DEC_FRAME_DURATION_60_MS;
    const esp_audio_err_t open_result =
        esp_opus_dec_open(&decoder_config, sizeof(decoder_config), &decoder);
    if (open_result != ESP_AUDIO_ERR_OK || decoder == nullptr) {
        ESP_LOGE(TAG, "Opus decoder open failed: %d", open_result);
        Show(display, "SPK ERROR", "decoder open failed");
        return false;
    }

    bool decoded = true;
    size_t packets = 0;
    auto demuxer = std::make_unique<OggDemuxer>();
    demuxer->OnDemuxerFinished([&](const uint8_t* data, int sample_rate, size_t size) {
        if (!decoded || sample_rate != kSpeakerTestSampleRate) {
            decoded = false;
            return;
        }
        std::vector<int16_t> pcm(sample_rate / 1000 * kOpusFrameDurationMs);
        esp_audio_dec_in_raw_t input = {
            .buffer = const_cast<uint8_t*>(data),
            .len = static_cast<uint32_t>(size),
            .consumed = 0,
            .frame_recover = ESP_AUDIO_DEC_RECOVERY_NONE,
        };
        esp_audio_dec_out_frame_t output = {
            .buffer = reinterpret_cast<uint8_t*>(pcm.data()),
            .len = static_cast<uint32_t>(pcm.size() * sizeof(int16_t)),
            .decoded_size = 0,
        };
        esp_audio_dec_info_t info = {};
        const esp_audio_err_t result = esp_opus_dec_decode(decoder, &input, &output, &info);
        if (result != ESP_AUDIO_ERR_OK) {
            ESP_LOGE(TAG, "Opus packet decode failed: %d", result);
            decoded = false;
            return;
        }
        ++packets;
        decoded = WritePcm(tx, pcm.data(), output.decoded_size / sizeof(int16_t), display);
    });
    const auto ogg = Lang::Sounds::OGG_WELCOME;
    demuxer->Process(reinterpret_cast<const uint8_t*>(ogg.data()), ogg.size());
    esp_opus_dec_close(decoder);
    ESP_LOGI(TAG, "welcome speech packets decoded=%zu result=%s", packets,
             decoded ? "ok" : "failed");
    return decoded && packets != 0;
}

void SpeakerTestTask(void* argument) {
    auto* display = static_cast<Display*>(argument);
    ESP_LOGI(TAG, "TX-only test: port=0 BCLK=%d LRCK=%d DOUT=%d slot=left; microphone unused",
             AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT);
    if (!WaitForBoot(display)) {
        vTaskDelete(nullptr);
        return;
    }
    LogStack("BOOT accepted");

    i2s_chan_handle_t tx = nullptr;
    i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(XIAOZHI_I2S_PORT(0), I2S_ROLE_MASTER);
    if (!Check(i2s_new_channel(&channel_config, &tx, nullptr), "I2S TX create", display)) {
        vTaskDelete(nullptr);
        return;
    }
    i2s_std_config_t config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSpeakerTestSampleRate),
        .slot_cfg =
            I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg =
            {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = AUDIO_I2S_SPK_GPIO_BCLK,
                .ws = AUDIO_I2S_SPK_GPIO_LRCK,
                .dout = AUDIO_I2S_SPK_GPIO_DOUT,
                .din = I2S_GPIO_UNUSED,
                .invert_flags = {},
            },
    };
    config.slot_cfg.slot_mask = AUDIO_I2S_SPK_SLOT;
    if (!Check(i2s_channel_init_std_mode(tx, &config), "I2S TX init", display) ||
        !Check(i2s_channel_enable(tx), "I2S TX enable", display)) {
        i2s_del_channel(tx);
        vTaskDelete(nullptr);
        return;
    }

    Show(display, "SPK TONE", "low-volume prompt");
    bool played = WriteSilence(tx, 300, display) && PlayPromptTone(tx, display) &&
                  WriteSilence(tx, 300, display);
    LogStack("prompt tone finished");
    if (played) {
        Show(display, "SPK VOICE", "low-volume speech");
        LogStack("starting welcome speech");
        played = PlayWelcomeSpeech(tx, display) && WriteSilence(tx, 500, display);
        LogStack("welcome speech finished");
    }
    Check(i2s_channel_disable(tx), "I2S TX disable", display);
    Check(i2s_del_channel(tx), "I2S TX delete", display);
    if (played) {
        ESP_LOGI(TAG, "speaker test finished; output channel disabled and silent");
        Show(display, "SPK DONE", "check noise and distortion");
    }
    vTaskDelete(nullptr);
}

}  // namespace

bool RunWaveshareSpeakerTest(Display* display) {
    const BaseType_t result =
        xTaskCreate(SpeakerTestTask, "speaker_test", kSpeakerTaskStackSize, display, 5, nullptr);
    if (result != pdPASS) {
        ESP_LOGE(TAG, "speaker test task creation failed");
        Show(display, "SPK ERROR", "task creation failed");
        return false;
    }
    return true;
}
