import json
import re
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


if __name__ == "__main__":
    unittest.main()
