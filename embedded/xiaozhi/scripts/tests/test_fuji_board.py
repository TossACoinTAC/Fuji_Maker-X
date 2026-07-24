import json
import re
import unittest
from pathlib import Path


_PROJECT_ROOT = Path(__file__).resolve().parents[2]
_BOARD_DIR = _PROJECT_ROOT / "main" / "boards" / "fuji-devkit-s3"


class FujiBoardConfigTests(unittest.TestCase):
    def test_gpio_assignments_avoid_reserved_pins(self):
        config = (_BOARD_DIR / "config.h").read_text(encoding="utf-8")
        assignments = {
            name: int(pin)
            for name, pin in re.findall(
                r"^#define\s+([A-Z0-9_]+GPIO[A-Z0-9_]*)\s+GPIO_NUM_(\d+)$",
                config,
                flags=re.MULTILINE,
            )
        }

        self.assertEqual(
            assignments,
            {
                "AUDIO_I2S_GPIO_BCLK": 17,
                "AUDIO_I2S_GPIO_WS": 18,
                "AUDIO_I2S_GPIO_DIN": 16,
                "AUDIO_I2S_GPIO_DOUT": 15,
                "AUDIO_AMP_ENABLE_GPIO": 8,
                "DISPLAY_MOSI_GPIO": 11,
                "DISPLAY_SCLK_GPIO": 12,
                "DISPLAY_CS_GPIO": 10,
                "DISPLAY_DC_GPIO": 13,
                "DISPLAY_RESET_GPIO": 14,
                "DISPLAY_BACKLIGHT_GPIO": 9,
                "USER_BUTTON_GPIO": 4,
            },
        )
        self.assertTrue(set(assignments.values()).isdisjoint({19, 20, 35, 36, 37, 43, 44, 48}))
        self.assertEqual(len(assignments), len(set(assignments.values())))

    def test_board_source_does_not_embed_numbered_gpio_constants(self):
        sources = "\n".join(
            path.read_text(encoding="utf-8") for path in sorted(_BOARD_DIR.glob("*.cc"))
        )
        self.assertNotRegex(sources, r"GPIO_NUM_\d+")

    def test_board_orchestration_is_split_from_hardware_modules(self):
        expected_modules = {
            "fuji_audio_codec.cc",
            "fuji_board_probe.cc",
            "fuji_display.cc",
            "fuji_microphone_test.cc",
        }
        self.assertTrue(expected_modules.issubset({path.name for path in _BOARD_DIR.glob("*.cc")}))

        board_source = (_BOARD_DIR / "fuji_devkit_s3.cc").read_text(encoding="utf-8")
        self.assertLess(len(board_source.splitlines()), 150)
        self.assertIn("FujiDisplay display_;", board_source)
        self.assertIn("RunFujiMicrophoneTest", board_source)

    def test_build_variants_are_pinned_to_idf_6_0_2(self):
        config = json.loads((_BOARD_DIR / "config.json").read_text(encoding="utf-8"))
        self.assertEqual(
            {build["name"] for build in config["builds"]},
            {
                "fuji-devkit-s3",
                "fuji-devkit-s3-probe",
                "fuji-devkit-s3-display-test",
                "fuji-devkit-s3-oled-test",
                "fuji-devkit-s3-mic-test",
                "fuji-devkit-s3-self-test",
            },
        )
        self.assertTrue(all(build["idf_version"] == "==6.0.2" for build in config["builds"]))
        probe = next(build for build in config["builds"] if build["name"].endswith("-probe"))
        self.assertIn("CONFIG_SPIRAM_IGNORE_NOTFOUND=y", probe["sdkconfig_append"])

    def test_probe_entry_does_not_construct_a_board_or_open_nvs(self):
        main = (_PROJECT_ROOT / "main" / "main.cc").read_text(encoding="utf-8")
        probe_branch = main.split("#if CONFIG_BOARD_PROBE_ONLY", maxsplit=2)[2].split(
            "#elif CONFIG_BOARD_DISPLAY_TEST_ONLY", maxsplit=1
        )[0]

        self.assertIn("RunFujiDevKitS3BoardProbe();", probe_branch)
        self.assertNotIn("Board::GetInstance()", probe_branch)
        self.assertNotIn("nvs_flash_init()", probe_branch)

    def test_display_test_stops_before_application_startup(self):
        main = (_PROJECT_ROOT / "main" / "main.cc").read_text(encoding="utf-8")
        display_branch = main.split("#elif CONFIG_BOARD_DISPLAY_TEST_ONLY", maxsplit=1)[1].split(
            "#elif CONFIG_BOARD_OLED_TEST_ONLY", maxsplit=1
        )[0]

        self.assertIn("Board::GetInstance();", display_branch)
        self.assertIn("IdleForever();", display_branch)
        self.assertNotIn("Application::GetInstance()", display_branch)

    def test_oled_test_stops_before_application_startup(self):
        main = (_PROJECT_ROOT / "main" / "main.cc").read_text(encoding="utf-8")
        oled_branch = main.split("#elif CONFIG_BOARD_OLED_TEST_ONLY", maxsplit=1)[1].split(
            "#elif CONFIG_BOARD_MIC_TEST_ONLY", maxsplit=1
        )[0]

        self.assertIn("Board::GetInstance();", oled_branch)
        self.assertIn("IdleForever();", oled_branch)
        self.assertNotIn("Application::GetInstance()", oled_branch)

    def test_mic_test_stops_before_application_startup(self):
        main = (_PROJECT_ROOT / "main" / "main.cc").read_text(encoding="utf-8")
        mic_branch = main.split("#elif CONFIG_BOARD_MIC_TEST_ONLY", maxsplit=1)[1].split(
            "#else", maxsplit=1
        )[0]

        self.assertIn("Board::GetInstance();", mic_branch)
        self.assertIn("IdleForever();", mic_branch)
        self.assertNotIn("Application::GetInstance()", mic_branch)

    def test_oled_test_scans_i2c_and_uses_shared_display_pins(self):
        config = (_BOARD_DIR / "config.h").read_text(encoding="utf-8")
        source = (_BOARD_DIR / "fuji_display.cc").read_text(encoding="utf-8")

        self.assertIn("#define OLED_SDA_GPIO DISPLAY_MOSI_GPIO", config)
        self.assertIn("#define OLED_SCL_GPIO DISPLAY_SCLK_GPIO", config)
        self.assertIn("#define OLED_WIDTH 128", config)
        self.assertIn("#define OLED_HEIGHT 32", config)
        self.assertIn("i2c_master_probe", source)
        self.assertIn("esp_lcd_new_panel_ssd1306", source)
        self.assertIn("OLED all-pixels-on frame", source)

    def test_mic_test_uses_rx_only_i2s_and_keeps_amplifier_disabled(self):
        mic_test = (_BOARD_DIR / "fuji_microphone_test.cc").read_text(encoding="utf-8")

        self.assertIn("i2s_new_channel(&channel_config, nullptr, &rx_handle)", mic_test)
        self.assertIn(".dout = I2S_GPIO_UNUSED", mic_test)
        self.assertIn("AUDIO_I2S_GPIO_DIN", mic_test)
        self.assertNotIn("EnableOutput", mic_test)
        self.assertNotIn("OutputData", mic_test)


if __name__ == "__main__":
    unittest.main()
