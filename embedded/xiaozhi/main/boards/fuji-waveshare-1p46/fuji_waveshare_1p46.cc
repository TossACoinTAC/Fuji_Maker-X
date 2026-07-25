#include "wifi_board.h"

#include "application.h"
#include "button.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "spd2010_touch.h"
#include "waveshare_display.h"
#include "waveshare_microphone_test.h"
#include "waveshare_peripherals.h"
#include "waveshare_speaker_test.h"

#include <memory>

class FujiWaveshare1p46 : public WifiBoard {
private:
    WavesharePeripherals peripherals_;
    Spd2010Touch touch_;
    WaveshareDisplay display_;
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
            } else {
                backlight->SetBrightness(0);
            }
        });
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
