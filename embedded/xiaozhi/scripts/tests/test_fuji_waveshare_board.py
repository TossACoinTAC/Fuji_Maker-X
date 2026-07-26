import json
import re
import struct
import unittest
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]
BOARD_DIR = PROJECT_ROOT / "main/boards/fuji-waveshare-1p46"


class FujiWaveshareBoardTests(unittest.TestCase):
    def test_pin_and_address_map_matches_schematic(self):
        config = (BOARD_DIR / "config.h").read_text(encoding="utf-8")
        expected = {
            "POWER_BUTTON_GPIO": 6,
            "POWER_HOLD_GPIO": 7,
            "RTC_INTERRUPT_GPIO": 9,
            "AUDIO_I2S_MIC_GPIO_WS": 2,
            "AUDIO_I2S_MIC_GPIO_BCLK": 15,
            "AUDIO_I2S_MIC_GPIO_DIN": 39,
            "AUDIO_I2S_SPK_GPIO_DOUT": 47,
            "AUDIO_I2S_SPK_GPIO_BCLK": 48,
            "AUDIO_I2S_SPK_GPIO_LRCK": 38,
            "BOARD_I2C_SCL_GPIO": 10,
            "BOARD_I2C_SDA_GPIO": 11,
            "DISPLAY_SPI_CS_GPIO": 21,
            "DISPLAY_SPI_SCLK_GPIO": 40,
            "DISPLAY_SPI_DATA0_GPIO": 46,
            "DISPLAY_SPI_DATA1_GPIO": 45,
            "DISPLAY_SPI_DATA2_GPIO": 42,
            "DISPLAY_SPI_DATA3_GPIO": 41,
            "DISPLAY_BACKLIGHT_GPIO": 5,
            "TOUCH_INTERRUPT_GPIO": 4,
        }
        for name, pin in expected.items():
            self.assertRegex(config, rf"#define {name} GPIO_NUM_{pin}\b")
        for name, address in {
            "TCA9554_I2C_ADDRESS": "0x20",
            "RTC_I2C_ADDRESS": "0x51",
            "TOUCH_I2C_ADDRESS": "0x53",
            "IMU_I2C_ADDRESS_LOW": "0x6A",
            "IMU_I2C_ADDRESS_HIGH": "0x6B",
        }.items():
            self.assertRegex(config, rf"#define {name} {address}\b")
        self.assertIn("#define DISPLAY_WIDTH 412", config)
        self.assertIn("#define DISPLAY_HEIGHT 412", config)
        self.assertIn("#define DISPLAY_SPI_CLOCK_HZ (40 * 1000 * 1000)", config)
        self.assertIn("#define DISPLAY_SPI_MODE 3", config)
        self.assertIn(
            "#define IMU_INTERRUPT_2_EXPANDER_PIN IO_EXPANDER_PIN_NUM_3", config
        )
        self.assertIn(
            "#define IMU_INTERRUPT_1_EXPANDER_PIN IO_EXPANDER_PIN_NUM_4", config
        )

    def test_exactly_five_idf_6_variants(self):
        config = json.loads((BOARD_DIR / "config.json").read_text(encoding="utf-8"))
        self.assertEqual(
            {build["name"] for build in config["builds"]},
            {
                "fuji-waveshare-1p46",
                "fuji-waveshare-1p46-probe",
                "fuji-waveshare-1p46-display-test",
                "fuji-waveshare-1p46-mic-test",
                "fuji-waveshare-1p46-speaker-test",
            },
        )
        self.assertTrue(all(build["idf_version"] == "==6.0.2" for build in config["builds"]))
        for build in config["builds"]:
            options = build["sdkconfig_append"]
            self.assertIn("CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y", options)
            self.assertIn("CONFIG_SPIRAM=y", options)
            self.assertIn("CONFIG_SPIRAM_MODE_OCT=y", options)

    def test_single_board_registration_and_private_implementation(self):
        sources = "\n".join(
            path.read_text(encoding="utf-8") for path in sorted(BOARD_DIR.glob("*.cc"))
        )
        self.assertEqual(len(re.findall(r"\bDECLARE_BOARD\(", sources)), 1)
        self.assertIn("DECLARE_BOARD(FujiWaveshare1p46);", sources)
        upstream = PROJECT_ROOT / "main/boards/waveshare/esp32-s3-touch-lcd-1.46"
        self.assertNotIn("fuji-waveshare-1p46", "\n".join(
            path.read_text(encoding="utf-8", errors="ignore")
            for path in upstream.rglob("*") if path.is_file()
        ))

    def test_probe_and_diagnostic_modes_stop_before_application(self):
        main = (PROJECT_ROOT / "main/main.cc").read_text(encoding="utf-8")
        probe = main.split("#if CONFIG_BOARD_PROBE_ONLY", maxsplit=2)[2].split(
            "#elif CONFIG_BOARD_DISPLAY_TEST_ONLY", maxsplit=1
        )[0]
        self.assertIn("RunBoardProbe();", probe)
        self.assertNotIn("InitializeNvs", probe)
        self.assertNotIn("Board::GetInstance", probe)
        for start, end in (
            ("CONFIG_BOARD_DISPLAY_TEST_ONLY", "CONFIG_BOARD_OLED_TEST_ONLY"),
            ("CONFIG_BOARD_MIC_TEST_ONLY", "CONFIG_BOARD_SPEAKER_TEST_ONLY"),
            ("CONFIG_BOARD_SPEAKER_TEST_ONLY", "#else"),
        ):
            branch = main.split(f"#elif {start}", maxsplit=1)[1].split(
                f"#elif {end}" if end != "#else" else end, maxsplit=1
            )[0]
            self.assertIn("IdleForever();", branch)
            self.assertNotIn("Application::GetInstance", branch)

    def test_display_touch_and_resets_are_isolated(self):
        board = (BOARD_DIR / "fuji_waveshare_1p46.cc").read_text(encoding="utf-8")
        peripherals = (BOARD_DIR / "waveshare_peripherals.cc").read_text(encoding="utf-8")
        display = (BOARD_DIR / "waveshare_display.cc").read_text(encoding="utf-8")
        touch = (BOARD_DIR / "spd2010_touch.cc").read_text(encoding="utf-8")
        self.assertLess(
            board.index("InitializeWavesharePowerHold()"),
            board.index("peripherals_.Initialize()"),
        )
        self.assertIn("ResetDisplay", peripherals)
        self.assertIn("ResetTouch", peripherals)
        self.assertLess(
            peripherals.index("esp_io_expander_new_i2c_tca9554"),
            peripherals.index("return ResetTouch() && ScanI2cBus()"),
        )
        self.assertNotIn("peripherals.ResetTouch()", display)
        self.assertIn("touch initialization will", peripherals)
        self.assertIn("ReadFirmwareVersion()", touch)
        self.assertNotIn(
            "DISPLAY_RESET_EXPANDER_PIN | TOUCH_RESET_EXPANDER_PIN", peripherals
        )
        self.assertIn("WireRgb565", display)
        self.assertIn("display self-test: red, green, blue, white, black", display)
        self.assertIn("touch center and four edges", display)
        self.assertIn("TOUCH_INTERRUPT_GPIO", touch)
        self.assertIn("TOUCH_I2C_ADDRESS", touch)

    def test_mic_is_rx_only_and_has_capture_partition(self):
        source = (BOARD_DIR / "waveshare_microphone_test.cc").read_text(encoding="utf-8")
        board = (BOARD_DIR / "fuji_waveshare_1p46.cc").read_text(encoding="utf-8")
        self.assertIn("i2s_new_channel(&channel_config, nullptr, &rx)", source)
        self.assertIn(".dout = I2S_GPIO_UNUSED", source)
        self.assertIn("I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG", source)
        self.assertNotIn("I2S_STD_MSB_SLOT_DEFAULT_CONFIG", source)
        self.assertIn("gpio_intr_disable(TOUCH_INTERRUPT_GPIO)", source)
        self.assertIn('"mic_capture"', source)
        self.assertIn('"MIC QUIET"', source)
        self.assertIn('"MIC SPEAK"', source)
        self.assertIn('"MIC CLAP"', source)
        self.assertIn("response_ratio", source)
        self.assertLess(
            board.index("PrepareWaveshareMicrophoneCapture()"),
            board.index("peripherals_.Initialize()"),
        )
        config = json.loads((BOARD_DIR / "config.json").read_text(encoding="utf-8"))
        mic = next(build for build in config["builds"] if build["name"].endswith("-mic-test"))
        self.assertIn(
            'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions/v2/16m_mic_capture.csv"',
            mic["sdkconfig_append"],
        )

    def test_speaker_is_tx_only_armed_and_low_gain(self):
        source = (BOARD_DIR / "waveshare_speaker_test.cc").read_text(encoding="utf-8")
        self.assertIn("i2s_new_channel(&channel_config, &tx, nullptr)", source)
        self.assertIn(".din = I2S_GPIO_UNUSED", source)
        self.assertIn("I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG", source)
        self.assertNotIn("I2S_STD_MSB_SLOT_DEFAULT_CONFIG", source)
        self.assertIn("kLowDigitalGain = 2048", source)
        self.assertIn("kSpeakerTaskStackSize = 32 * 1024", source)
        self.assertIn('xTaskCreate(SpeakerTestTask, "speaker_test"', source)
        self.assertIn("std::make_unique<OggDemuxer>()", source)
        self.assertIn("minimum remaining stack", source)
        self.assertIn("WaitForBoot", source)
        self.assertLess(source.index("WaitForBoot(display)"), source.index("i2s_new_channel"))
        self.assertIn("i2s_channel_disable", source)
        self.assertIn("OGG_WELCOME", source)

    def test_motion_and_rtc_drivers_are_private_and_conservative(self):
        board = (BOARD_DIR / "fuji_waveshare_1p46.cc").read_text(encoding="utf-8")
        imu = (BOARD_DIR / "waveshare_imu.cc").read_text(encoding="utf-8")
        rtc = (BOARD_DIR / "waveshare_rtc.cc").read_text(encoding="utf-8")
        self.assertIn("InitializeMotionAndClock", board)
        normal_branch = board.split("#else", maxsplit=1)[1].split("#endif", maxsplit=1)[0]
        self.assertIn("InitializeMotionAndClock();", normal_branch)
        self.assertNotIn("InitializeMotionAndClock();", board.split("#else", maxsplit=1)[0])
        self.assertIn("kExpectedWhoAmI = 0x05", imu)
        self.assertIn("kAcceleration4gAt120Hz = 0x16", imu)
        self.assertIn("kGyroscope256dpsAt120Hz = 0x46", imu)
        self.assertIn("kEnableAccelerationAndGyroscope = 0x43", imu)
        self.assertIn("acceleration_sum > 0.1f", imu)
        self.assertIn("ReadSample", imu)
        self.assertIn("PCF85063 read-only time", rtc)
        self.assertIn("2000 + BcdToDecimal(year)", rtc)
        self.assertNotIn("i2c_master_transmit(", rtc)

    def test_power_key_shutdown_mutes_before_releasing_hold(self):
        board = (BOARD_DIR / "fuji_waveshare_1p46.cc").read_text(encoding="utf-8")
        peripherals = (BOARD_DIR / "waveshare_peripherals.cc").read_text(encoding="utf-8")
        shutdown = board.split("power_button_->OnLongPress", maxsplit=1)[1].split(
            "void InitializeMotionAndClock", maxsplit=1
        )[0]
        self.assertIn("Application::GetInstance().Schedule", shutdown)
        self.assertIn("audio_service.Stop()", shutdown)
        self.assertIn("esp_sleep_enable_ext1_wakeup_io", shutdown)
        self.assertIn("ESP_EXT1_WAKEUP_ANY_LOW", shutdown)
        self.assertIn("esp_deep_sleep_start()", shutdown)
        release = shutdown.index("ReleaseWavesharePowerHold")
        self.assertLess(shutdown.index("audio_service.Stop()"), release)
        self.assertLess(shutdown.index("EnableOutput(false)"), release)
        self.assertLess(shutdown.index("EnableInput(false)"), release)
        self.assertLess(shutdown.index("SetBrightness(0)"), release)
        self.assertIn("gpio_set_level(POWER_HOLD_GPIO, 0)", peripherals)
        constructor = board.split("FujiWaveshare1p46() {", maxsplit=1)[1]
        self.assertLess(
            constructor.index("InitializeWavesharePowerHold()"),
            constructor.index("InitializeMotionAndClock()"),
        )

    def test_expression_assets_are_rgba_320_square_placeholders(self):
        assets = BOARD_DIR / "assets"
        expected = {
            f"fuji_{state}.png"
            for state in (
                "idle",
                "listening",
                "thinking",
                "connecting",
                "speaking",
                "success",
                "error",
                "offline",
                "muted",
            )
        }
        self.assertEqual({path.name for path in assets.glob("*.png")}, expected)
        for path in assets.glob("*.png"):
            data = path.read_bytes()
            self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
            width, height = struct.unpack(">II", data[16:24])
            self.assertEqual((width, height), (320, 320))
            self.assertIn(data[25], (4, 6), f"{path.name} must contain alpha")

    def test_expression_controller_is_private_prioritized_and_power_aware(self):
        cmake = (PROJECT_ROOT / "main/CMakeLists.txt").read_text(encoding="utf-8")
        controller = (BOARD_DIR / "fuji_expression_controller.cc").read_text(
            encoding="utf-8"
        )
        policy = (BOARD_DIR / "fuji_expression_policy.cc").read_text(encoding="utf-8")
        board = (BOARD_DIR / "fuji_waveshare_1p46.cc").read_text(encoding="utf-8")
        display = (BOARD_DIR / "waveshare_display.cc").read_text(encoding="utf-8")

        self.assertIn("boards/fuji-waveshare-1p46/assets", cmake)
        self.assertIn("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT", controller)
        expression_header = (BOARD_DIR / "fuji_expression_controller.h").read_text(
            encoding="utf-8"
        )
        self.assertIn("kFramePeriodMs = 83", expression_header)
        self.assertIn("PrewarmCoreAssets();", controller)
        self.assertIn("lv_refr_now(nullptr)", controller)
        self.assertIn("esp_timer_get_time()", controller)
        self.assertIn("kMetricsPeriodUs", expression_header)
        self.assertNotIn("kMetricsPeriodTicks", expression_header)
        self.assertIn("warm_baseline", controller)
        set_hint = controller.split(
            "void FujiExpressionController::SetServerEmotionHint", maxsplit=1
        )[1].split("void FujiExpressionController::SetScreenEnabled", maxsplit=1)[0]
        self.assertIn("hint_.store", set_hint)
        self.assertNotIn("Tick(", set_hint)
        set_emotion = display.split(
            "void SetEmotion(const char* emotion) override", maxsplit=1
        )[1].split("void SetPowerSaveMode", maxsplit=1)[0]
        self.assertNotIn("DisplayLockGuard", set_emotion)
        self.assertLess(policy.index("!inputs.screen_enabled"), policy.index("inputs.fatal_error"))
        self.assertLess(policy.index("inputs.fatal_error"), policy.index("inputs.offline"))
        self.assertLess(policy.index("inputs.offline"), policy.index("inputs.muted"))
        self.assertLess(policy.index("inputs.muted"), policy.index("inputs.activity"))
        self.assertIn("expression_->SetScreenEnabled(!on)", display)

        backlight_off = board.split("if (backlight->brightness() == 0)", maxsplit=1)[1]
        self.assertLess(
            backlight_off.index("SetPowerSaveMode(true)"),
            backlight_off.index("SetBrightness(0)"),
        )

    def test_barge_in_stops_playback_before_listening_and_has_touch_fallback(self):
        application = (PROJECT_ROOT / "main/application.cc").read_text(encoding="utf-8")
        application_header = (PROJECT_ROOT / "main/application.h").read_text(
            encoding="utf-8"
        )
        audio = (PROJECT_ROOT / "main/audio/audio_service.cc").read_text(
            encoding="utf-8"
        )
        audio_header = (PROJECT_ROOT / "main/audio/audio_service.h").read_text(
            encoding="utf-8"
        )
        display = (BOARD_DIR / "waveshare_display.cc").read_text(encoding="utf-8")
        controller = (BOARD_DIR / "fuji_expression_controller.cc").read_text(
            encoding="utf-8"
        )
        controller_header = (BOARD_DIR / "fuji_expression_controller.h").read_text(
            encoding="utf-8"
        )

        self.assertIn("MAIN_EVENT_BARGE_IN", application_header)
        self.assertIn("MAIN_EVENT_VOICE_CAPTURE_READY", application_header)
        self.assertIn("PerformBargeIn(kAbortReasonWakeWordDetected", application)
        barge_in = application.split("void Application::PerformBargeIn", maxsplit=1)[1].split(
            "void Application::SetListeningMode", maxsplit=1
        )[0]
        self.assertLess(barge_in.index("AbortSpeaking(reason)"), barge_in.index("SetListeningMode"))
        self.assertLess(barge_in.index("SetListeningMode"), barge_in.index("InterruptPlayback()"))
        self.assertIn('"barge-in playback silent latency=%lld ms"', application)
        self.assertIn('"barge-in microphone ready latency=%lld ms"', application)

        self.assertIn("void InterruptPlayback();", audio_header)
        interrupt = audio.split("void AudioService::InterruptPlayback()", maxsplit=1)[1].split(
            "bool AudioService::IsPlaybackDrainedLocked", maxsplit=1
        )[0]
        self.assertLess(interrupt.index("ResetDecoder()"), interrupt.index("EnableOutput(false)"))
        self.assertIn("on_voice_capture_ready", audio_header)
        self.assertIn("voice_capture_ready_pending_.exchange(false)", audio)
        self.assertIn("task->playback_generation == playback_generation_.load()", audio)
        self.assertIn("barge_in_waiting_for_capture_ ? 0 : 120", application)

        self.assertIn("Application::GetInstance().BargeIn();", display)
        self.assertIn("kTouchDebounceUs", display)
        self.assertIn("kDeviceStateSpeaking", display)
        self.assertIn("FujiExpression::kInterrupting", controller)
        self.assertIn("TriggerBargeInTransition", controller)
        self.assertIn("SetBargeInTransition", application)
        self.assertIn("kInterruptingDurationUs = 166 * 1000", controller_header)

    def test_ble_transport_is_secure_private_and_excluded_from_diagnostics(self):
        board_config = json.loads((BOARD_DIR / "config.json").read_text(encoding="utf-8"))
        normal = next(
            build for build in board_config["builds"]
            if build["name"] == "fuji-waveshare-1p46"
        )
        expected = {
            "CONFIG_FUJI_BLE_TRANSPORT=y",
            "CONFIG_BT_ENABLED=y",
            "CONFIG_BT_NIMBLE_ENABLED=y",
            "CONFIG_BT_CONTROLLER_ENABLED=y",
            "CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y",
            "CONFIG_BT_NIMBLE_ROLE_CENTRAL=n",
            "CONFIG_BT_NIMBLE_ROLE_OBSERVER=n",
            "CONFIG_BT_NIMBLE_SM_LEGACY=n",
            "CONFIG_BT_NIMBLE_SM_SC=y",
            "CONFIG_BT_NIMBLE_SM_SC_ONLY=1",
            "CONFIG_BT_NIMBLE_SM_LVL=3",
            "CONFIG_BT_NIMBLE_NVS_PERSIST=y",
            "CONFIG_BT_NIMBLE_MAX_BONDS=2",
            "CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1",
            "CONFIG_BT_NIMBLE_MAX_CCCDS=2",
            "CONFIG_BT_NIMBLE_50_FEATURE_SUPPORT=n",
            "CONFIG_BT_NIMBLE_DTM_MODE_TEST=n",
            "CONFIG_BT_CTRL_BLE_MAX_ACT=2",
            "CONFIG_BT_CTRL_DTM_ENABLE=n",
            "CONFIG_BT_CTRL_BLE_SCAN=n",
        }
        self.assertTrue(expected.issubset(set(normal["sdkconfig_append"])))
        for build in board_config["builds"]:
            if build is normal:
                continue
            self.assertFalse(any(
                option.startswith(("CONFIG_BT_", "CONFIG_FUJI_BLE_TRANSPORT="))
                for option in build["sdkconfig_append"]
            ))

        kconfig = (PROJECT_ROOT / "main/Kconfig.projbuild").read_text(encoding="utf-8")
        ble_gate = kconfig.split("config FUJI_BLE_TRANSPORT", maxsplit=1)[1].split(
            "config BOARD_HARDWARE_SELF_TEST", maxsplit=1
        )[0]
        for diagnostic in (
            "BOARD_PROBE_ONLY",
            "BOARD_DISPLAY_TEST_ONLY",
            "BOARD_MIC_TEST_ONLY",
            "BOARD_SPEAKER_TEST_ONLY",
        ):
            self.assertIn(f"!{diagnostic}", ble_gate)

        cmake = (PROJECT_ROOT / "main/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn(
            "CONFIG_FUJI_BLE_TRANSPORT requires the Bluetooth controller and NimBLE host",
            cmake,
        )

        transport = (PROJECT_ROOT / "main/fuji_ble/fuji_ble_transport.cc").read_text(
            encoding="utf-8"
        )
        for uuid_tail in (
            "0x22, 0xbc, 0xa5, 0x0d",
            "0x20, 0xfc, 0x3f, 0xa7",
            "0x12, 0xb9, 0x7e, 0xa9",
            "0xfd, 0xd1, 0xaf, 0xd0",
        ):
            self.assertIn(uuid_tail, transport)
        self.assertIn("BLE_HS_IO_DISPLAY_YESNO", transport)
        self.assertIn("BLE_GATT_CHR_F_WRITE_AUTHEN", transport)
        self.assertIn("BLE_GATT_CHR_F_NOTIFY_INDICATE_AUTHEN", transport)
        self.assertIn("kPairingWindowUs = 120LL * 1000 * 1000", transport)
        self.assertIn("kMaximumAdvertisingBackoffMs = 60000", transport)
        self.assertIn("KeepOnlyPeerBond(event->enc_change.conn_handle)", transport)
        self.assertIn("ble_store_util_delete_peer(&peers[index])", transport)

        board = (BOARD_DIR / "fuji_waveshare_1p46.cc").read_text(encoding="utf-8")
        self.assertIn("Button>(BOOT_BUTTON_GPIO, false, 2000)", board)
        self.assertIn("ConfirmPendingComparison()", board)
        self.assertIn("EnterPairingMode()", board)
        application = (PROJECT_ROOT / "main/application.cc").read_text(encoding="utf-8")
        self.assertIn("make_state_snapshot", application)
        self.assertIn("PublishStateSnapshot()", application)
        self.assertIn("PAIR %06lu - PRESS BOOT", application)


if __name__ == "__main__":
    unittest.main()
