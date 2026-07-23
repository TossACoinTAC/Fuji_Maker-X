#include "wifi_board.h"

#include "application.h"
#include "assets/lang_config.h"
#include "button.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "display/lcd_display.h"

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_idf_version.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_lcd_st7735.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#define TAG "FujiDevKitS3"

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

void LogBoardProbe() {
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

}  // namespace

class FujiAudioCodec : public NoAudioCodecDuplex {
private:
    std::atomic_bool software_muted_{false};

protected:
    int Read(int16_t* dest, int samples) override {
        if (!software_muted_.load()) {
            return NoAudioCodec::Read(dest, samples);
        }

        std::fill_n(dest, samples, 0);
        const int delay_ms = std::max(1, samples * 1000 / input_sample_rate_);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        return samples;
    }

    void EnableOutput(bool enable) override {
        if (!enable) {
            gpio_set_level(AUDIO_AMP_ENABLE_GPIO, 0);
        }
        NoAudioCodec::EnableOutput(enable);
        if (enable) {
            gpio_set_level(AUDIO_AMP_ENABLE_GPIO, 1);
        }
    }

public:
    FujiAudioCodec()
        : NoAudioCodecDuplex(
              AUDIO_INPUT_SAMPLE_RATE,
              AUDIO_OUTPUT_SAMPLE_RATE,
              AUDIO_I2S_GPIO_BCLK,
              AUDIO_I2S_GPIO_WS,
              AUDIO_I2S_GPIO_DOUT,
              AUDIO_I2S_GPIO_DIN) {
    }

    bool ToggleSoftwareMute() {
        const bool muted = !software_muted_.load();
        software_muted_.store(muted);
        ESP_LOGI(TAG, "software microphone mute=%s", muted ? "true" : "false");
        return muted;
    }

    void RunSelfTest() {
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
};

class FujiDevKitS3 : public WifiBoard {
private:
    Display* display_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    std::unique_ptr<Button> user_button_;

    FujiAudioCodec* GetFujiAudioCodec() {
        static FujiAudioCodec audio_codec;
        return &audio_codec;
    }

    void InitializeAmpSafeState() {
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

    void InitializeButton() {
        user_button_ = std::make_unique<Button>(USER_BUTTON_GPIO, false, 1500);
        user_button_->OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
        user_button_->OnLongPress([this]() {
            const bool muted = GetFujiAudioCodec()->ToggleSoftwareMute();
            auto display = GetDisplay();
            if (display->IsSetupUICalled()) {
                display->ShowNotification(muted ? Lang::Strings::MUTED : "MIC ON");
            }
        });
    }

    bool CheckDisplayStep(esp_err_t result, const char* step) {
        if (result == ESP_OK) {
            return true;
        }
        ESP_LOGE(TAG, "display %s failed: %s; continuing without a display",
                 step, esp_err_to_name(result));
        return false;
    }

    bool DrawSolidColor(uint16_t rgb565) {
        std::array<uint16_t, DISPLAY_WIDTH> line;
        std::fill(line.begin(), line.end(), rgb565);

        for (int y = 0; y < DISPLAY_HEIGHT; ++y) {
            const esp_err_t result = esp_lcd_panel_draw_bitmap(
                panel_,
                DISPLAY_OFFSET_X,
                DISPLAY_OFFSET_Y + y,
                DISPLAY_OFFSET_X + DISPLAY_WIDTH,
                DISPLAY_OFFSET_Y + y + 1,
                line.data());
            if (result != ESP_OK) {
                ESP_LOGE(TAG, "display color fill failed: %s", esp_err_to_name(result));
                return false;
            }
        }
        return true;
    }

    void RunDisplaySelfTest() {
#if CONFIG_BOARD_HARDWARE_SELF_TEST
        ESP_LOGI(TAG, "display self-test: red, green, blue, white");
        constexpr uint16_t kColors[] = {0xF800, 0x07E0, 0x001F, 0xFFFF};
        for (uint16_t color : kColors) {
            if (!DrawSolidColor(color)) {
                return;
            }
            vTaskDelay(pdMS_TO_TICKS(350));
        }
#endif
    }

    bool InitializeDisplay() {
        gpio_config_t backlight_config = {};
        backlight_config.pin_bit_mask = 1ULL << DISPLAY_BACKLIGHT_GPIO;
        backlight_config.mode = GPIO_MODE_OUTPUT;
        if (!CheckDisplayStep(gpio_config(&backlight_config), "backlight GPIO setup")) {
            return false;
        }
        gpio_set_level(DISPLAY_BACKLIGHT_GPIO, DISPLAY_BACKLIGHT_OUTPUT_INVERT ? 1 : 0);

        spi_bus_config_t bus_config = {};
        bus_config.mosi_io_num = DISPLAY_MOSI_GPIO;
        bus_config.miso_io_num = GPIO_NUM_NC;
        bus_config.sclk_io_num = DISPLAY_SCLK_GPIO;
        bus_config.quadwp_io_num = GPIO_NUM_NC;
        bus_config.quadhd_io_num = GPIO_NUM_NC;
        bus_config.max_transfer_sz = DISPLAY_WIDTH * 20 * sizeof(uint16_t);
        if (!CheckDisplayStep(
                spi_bus_initialize(DISPLAY_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO),
                "SPI bus setup")) {
            return false;
        }

        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_GPIO;
        io_config.dc_gpio_num = DISPLAY_DC_GPIO;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = DISPLAY_SPI_CLOCK_HZ;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        if (!CheckDisplayStep(
                esp_lcd_new_panel_io_spi(DISPLAY_SPI_HOST, &io_config, &panel_io_),
                "panel IO setup")) {
            return false;
        }

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RESET_GPIO;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        if (!CheckDisplayStep(
                esp_lcd_new_panel_st7735(panel_io_, &panel_config, &panel_),
                "ST7735 driver setup")) {
            return false;
        }

        if (!CheckDisplayStep(esp_lcd_panel_reset(panel_), "reset") ||
            !CheckDisplayStep(esp_lcd_panel_init(panel_), "initialization") ||
            !CheckDisplayStep(
                esp_lcd_panel_invert_color(panel_, DISPLAY_INVERT_COLOR),
                "color inversion") ||
            !CheckDisplayStep(
                esp_lcd_panel_swap_xy(panel_, DISPLAY_SWAP_XY),
                "axis configuration") ||
            !CheckDisplayStep(
                esp_lcd_panel_mirror(panel_, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y),
                "mirror configuration")) {
            return false;
        }

        ESP_LOGW(TAG, "ST7735 SPI initialized, but this write-only bus cannot verify panel presence");
        RunDisplaySelfTest();
        display_ = new SpiLcdDisplay(
            panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT,
            DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
            DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
        return display_ != nullptr;
    }

public:
    FujiDevKitS3() {
        LogBoardProbe();
#if !CONFIG_BOARD_PROBE_ONLY
        InitializeAmpSafeState();
        if (InitializeDisplay()) {
            GetBacklight()->RestoreBrightness();
        }
        InitializeButton();
#if CONFIG_BOARD_HARDWARE_SELF_TEST
        GetFujiAudioCodec()->RunSelfTest();
#endif
#endif
    }

    AudioCodec* GetAudioCodec() override {
        return GetFujiAudioCodec();
    }

    Display* GetDisplay() override {
        return display_ != nullptr ? display_ : Board::GetDisplay();
    }

    Backlight* GetBacklight() override {
        static PwmBacklight backlight(
            DISPLAY_BACKLIGHT_GPIO,
            DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }
};

DECLARE_BOARD(FujiDevKitS3);
