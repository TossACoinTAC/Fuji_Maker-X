#include "wifi_board.h"

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
#include <cinttypes>

#define TAG "FujiDevKitS3"

namespace {

void LogBoardProbe() {
    esp_chip_info_t chip_info = {};
    esp_chip_info(&chip_info);

    uint32_t flash_size = 0;
    const esp_err_t flash_result = esp_flash_get_size(nullptr, &flash_size);
    const size_t psram_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);

    ESP_LOGI(TAG, "board=fuji-devkit-s3 idf=%s", esp_get_idf_version());
    ESP_LOGI(TAG, "chip model=%d cores=%d revision=%d features=0x%" PRIx32,
             chip_info.model, chip_info.cores, chip_info.revision, chip_info.features);
    if (flash_result == ESP_OK) {
        ESP_LOGI(TAG, "flash=%" PRIu32 " bytes (%" PRIu32 " MiB)",
                 flash_size, flash_size / (1024 * 1024));
    } else {
        ESP_LOGE(TAG, "flash size query failed: %s", esp_err_to_name(flash_result));
    }
    ESP_LOGI(TAG, "psram=%zu bytes (%zu MiB)", psram_size, psram_size / (1024 * 1024));
    ESP_LOGI(TAG, "reset_reason=%d", static_cast<int>(esp_reset_reason()));

    if (flash_size != 16 * 1024 * 1024) {
        ESP_LOGW(TAG, "expected 16 MiB flash for N16R8; verify the module marking and build config");
    }
    if (psram_size != 8 * 1024 * 1024) {
        ESP_LOGW(TAG, "expected 8 MiB PSRAM for N16R8; verify Octal PSRAM configuration");
    }
}

}  // namespace

class FujiDevKitS3 : public WifiBoard {
private:
    Display* display_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

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

        ESP_LOGI(TAG, "ST7735 SPI initialized; this write-only bus cannot detect panel presence");
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
        if (InitializeDisplay()) {
            GetBacklight()->RestoreBrightness();
        }
#endif
    }

    AudioCodec* GetAudioCodec() override {
        static NoAudioCodecDuplex audio_codec(
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN);
        return &audio_codec;
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
