#include "wifi_board.h"

#include "application.h"
#include "assets/lang_config.h"
#include "board_probe.h"
#include "button.h"
#include "config.h"
#include "fuji_audio_codec.h"
#include "fuji_display.h"
#include "fuji_microphone_test.h"

#include <memory>

class FujiDevKitS3 : public WifiBoard {
private:
    FujiDisplay display_;
    std::unique_ptr<Button> user_button_;

    FujiAudioCodec* GetFujiAudioCodec() {
        static FujiAudioCodec audio_codec;
        return &audio_codec;
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

public:
    FujiDevKitS3() {
        RunBoardProbe();
#if !CONFIG_BOARD_PROBE_ONLY
#if !CONFIG_BOARD_DISPLAY_TEST_ONLY && !CONFIG_BOARD_OLED_TEST_ONLY
        InitializeFujiAmplifierSafeState();
#endif
#if CONFIG_BOARD_OLED_TEST_ONLY || CONFIG_BOARD_MIC_TEST_ONLY
        display_.InitializeOled();
#else
        if (display_.InitializeSt7735()) {
#if !CONFIG_BOARD_DISPLAY_TEST_ONLY
            GetBacklight()->RestoreBrightness();
#endif
        }
#endif
#if CONFIG_BOARD_MIC_TEST_ONLY
        RunFujiMicrophoneTest(display_.GetDisplay());
#elif !CONFIG_BOARD_DISPLAY_TEST_ONLY && !CONFIG_BOARD_OLED_TEST_ONLY
        InitializeButton();
#if CONFIG_BOARD_HARDWARE_SELF_TEST
        GetFujiAudioCodec()->RunSelfTest();
#endif
#endif
#endif
    }

    AudioCodec* GetAudioCodec() override {
        return GetFujiAudioCodec();
    }

    Display* GetDisplay() override {
        return display_.GetDisplay() != nullptr ? display_.GetDisplay() : Board::GetDisplay();
    }

    Backlight* GetBacklight() override {
        static PwmBacklight backlight(
            DISPLAY_BACKLIGHT_GPIO,
            DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }
};

DECLARE_BOARD(FujiDevKitS3);
