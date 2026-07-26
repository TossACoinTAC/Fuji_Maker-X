#include "wifi_board.h"

#include "application.h"
#include "button.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "display/display.h"
#include "spd2010_touch.h"
#include "waveshare_display.h"
#include "waveshare_microphone_test.h"
#include "waveshare_imu.h"
#include "waveshare_peripherals.h"
#include "waveshare_rtc.h"
#include "waveshare_speaker_test.h"

#include <esp_log.h>
#include <esp_sleep.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <memory>

#define TAG "FujiWsBoard"

class FujiWaveshare1p46 : public WifiBoard {
private:
    WavesharePeripherals peripherals_;
    Spd2010Touch touch_;
    WaveshareDisplay display_;
    WaveshareImu imu_;
    WaveshareRtc rtc_;
    std::unique_ptr<Button> boot_button_;
    std::unique_ptr<Button> power_button_;

    void InitializeButtons() {
        boot_button_ = std::make_unique<Button>(BOOT_BUTTON_GPIO, false, 1500);
        boot_button_->OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });

        power_button_ = std::make_unique<Button>(POWER_BUTTON_GPIO, false, 3000);
        power_button_->OnClick([this]() {
            auto* backlight = GetBacklight();
            if (backlight->brightness() == 0) {
                backlight->RestoreBrightness();
                GetDisplay()->SetPowerSaveMode(false);
            } else {
                GetDisplay()->SetPowerSaveMode(true);
                backlight->SetBrightness(0);
            }
        });
        power_button_->OnLongPress([this]() {
            Application::GetInstance().Schedule([this]() {
                ESP_LOGW(TAG, "power key held for 3 seconds; beginning controlled shutdown");
                auto& audio_service = Application::GetInstance().GetAudioService();
                audio_service.Stop();
                vTaskDelay(pdMS_TO_TICKS(100));
                auto* codec = GetAudioCodec();
                codec->EnableOutput(false);
                codec->EnableInput(false);
                GetDisplay()->SetStatus("POWER OFF");
                vTaskDelay(pdMS_TO_TICKS(250));
                GetDisplay()->SetPowerSaveMode(true);
                GetBacklight()->SetBrightness(0);

                while (gpio_get_level(POWER_BUTTON_GPIO) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup_io(
                    1ULL << POWER_BUTTON_GPIO, ESP_EXT1_WAKEUP_ANY_LOW));
                ReleaseWavesharePowerHold();
                vTaskDelay(pdMS_TO_TICKS(50));
                esp_deep_sleep_start();
            });
        });
    }

    void InitializeMotionAndClock() {
        if (!imu_.Initialize(peripherals_.i2c_bus())) {
            ESP_LOGW(TAG, "QMI8658 unavailable; continuing without motion data");
        }
        if (!rtc_.Initialize(peripherals_.i2c_bus())) {
            ESP_LOGW(TAG, "PCF85063 unavailable; continuing without RTC data");
        }
    }

public:
    FujiWaveshare1p46() {
        InitializeWavesharePowerHold();
#if CONFIG_BOARD_MIC_TEST_ONLY
        if (!PrepareWaveshareMicrophoneCapture()) {
            return;
        }
#endif
        if (!peripherals_.Initialize() || !display_.Initialize(peripherals_, touch_)) {
            return;
        }
#if CONFIG_BOARD_DISPLAY_TEST_ONLY
        display_.RunDisplayAndTouchTest(GetBacklight());
#elif CONFIG_BOARD_MIC_TEST_ONLY
        GetBacklight()->SetBrightness(20);
        display_.SetupDiagnosticUi("MIC TEST", "press BOOT to record");
        RunWaveshareMicrophoneTest(display_.GetDisplay());
#elif CONFIG_BOARD_SPEAKER_TEST_ONLY
        GetBacklight()->SetBrightness(20);
        display_.SetupDiagnosticUi("SPK TEST", "set volume min");
        RunWaveshareSpeakerTest(display_.GetDisplay());
#else
        GetBacklight()->RestoreBrightness();
        InitializeMotionAndClock();
        InitializeButtons();
#endif
    }

    AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                                               AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK,
                                               AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_SPK_SLOT,
                                               AUDIO_I2S_MIC_GPIO_BCLK, AUDIO_I2S_MIC_GPIO_WS,
                                               AUDIO_I2S_MIC_GPIO_DIN, AUDIO_I2S_MIC_SLOT);
        return &audio_codec;
    }

    Display* GetDisplay() override {
        return display_.GetDisplay() != nullptr ? display_.GetDisplay() : Board::GetDisplay();
    }

    Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_GPIO, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }
};

DECLARE_BOARD(FujiWaveshare1p46);
