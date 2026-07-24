#include "fuji_display.h"

#include "config.h"
#include "display/lcd_display.h"
#include "display/oled_display.h"

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_ssd1306.h>
#include <esp_lcd_st7735.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <array>

#define TAG "FujiDisplay"

bool FujiDisplay::CheckStep(esp_err_t result, const char* step) {
    if (result == ESP_OK) {
        return true;
    }
    ESP_LOGE(TAG, "display %s failed: %s; continuing without a display",
             step, esp_err_to_name(result));
    return false;
}

int FujiDisplay::ScanOledAddress() {
    int oled_address = -1;
    int device_count = 0;
    ESP_LOGI(TAG, "OLED I2C scan: SDA=%d SCL=%d", OLED_SDA_GPIO, OLED_SCL_GPIO);
    for (uint8_t address = 0x08; address <= 0x77; ++address) {
        const esp_err_t result = i2c_master_probe(i2c_bus_, address, 50);
        if (result != ESP_OK) {
            continue;
        }

        ++device_count;
        ESP_LOGI(TAG, "I2C device found at 0x%02X", address);
        if (oled_address < 0 &&
            (address == OLED_PRIMARY_ADDRESS || address == OLED_ALTERNATE_ADDRESS)) {
            oled_address = address;
        }
    }

    if (device_count == 0) {
        ESP_LOGE(TAG, "OLED I2C scan found no devices; check GND, 3V3, SCL and SDA");
    } else if (oled_address < 0) {
        ESP_LOGE(TAG, "I2C devices responded, but no OLED was found at 0x%02X or 0x%02X",
                 OLED_PRIMARY_ADDRESS, OLED_ALTERNATE_ADDRESS);
    }
    return oled_address;
}

bool FujiDisplay::DrawOledFill(uint8_t value, const char* step) {
    std::array<uint8_t, OLED_WIDTH * OLED_HEIGHT / 8> frame;
    frame.fill(value);
    return CheckStep(
        esp_lcd_panel_draw_bitmap(panel_, 0, 0, OLED_WIDTH, OLED_HEIGHT, frame.data()),
        step);
}

bool FujiDisplay::InitializeOled() {
    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = OLED_I2C_PORT;
    bus_config.sda_io_num = OLED_SDA_GPIO;
    bus_config.scl_io_num = OLED_SCL_GPIO;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;
    if (!CheckStep(i2c_new_master_bus(&bus_config, &i2c_bus_), "OLED I2C bus setup")) {
        return false;
    }

    const int address = ScanOledAddress();
    if (address < 0) {
        return false;
    }

    esp_lcd_panel_io_i2c_config_t io_config = {};
    io_config.dev_addr = static_cast<uint32_t>(address);
    io_config.scl_speed_hz = OLED_I2C_CLOCK_HZ;
    io_config.control_phase_bytes = 1;
    io_config.dc_bit_offset = 6;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    if (!CheckStep(
            esp_lcd_new_panel_io_i2c(i2c_bus_, &io_config, &panel_io_),
            "OLED panel IO setup")) {
        return false;
    }

    esp_lcd_panel_ssd1306_config_t ssd1306_config = {};
    ssd1306_config.height = OLED_HEIGHT;
    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = GPIO_NUM_NC;
    panel_config.bits_per_pixel = 1;
    panel_config.vendor_config = &ssd1306_config;
    if (!CheckStep(
            esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_),
            "SSD1306 driver setup") ||
        !CheckStep(esp_lcd_panel_reset(panel_), "OLED reset") ||
        !CheckStep(esp_lcd_panel_init(panel_), "OLED initialization") ||
        !CheckStep(esp_lcd_panel_disp_on_off(panel_, true), "OLED power-on")) {
        return false;
    }

    ESP_LOGI(TAG, "SSD1306 128x32 initialized at I2C address 0x%02X", address);
    if (!DrawOledFill(0xFF, "OLED all-pixels-on frame")) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(700));
    if (!DrawOledFill(0x00, "OLED all-pixels-off frame")) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(350));

    display_ = new OledDisplay(
        panel_io_, panel_, OLED_WIDTH, OLED_HEIGHT, OLED_MIRROR_X, OLED_MIRROR_Y);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "failed to allocate the OLED display interface");
        return false;
    }
    display_->SetupUI();
    display_->SetStatus("OLED OK");
    display_->SetChatMessage("system", "I2C SSD1306 128x32");
    ESP_LOGI(TAG, "OLED test is holding the 128x32 Xiaozhi status layout");
    return true;
}

bool FujiDisplay::DrawSolidColor(uint16_t rgb565) {
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

void FujiDisplay::RunSt7735SelfTest() {
#if CONFIG_BOARD_HARDWARE_SELF_TEST || CONFIG_BOARD_DISPLAY_TEST_ONLY
    ESP_LOGI(TAG, "display self-test: red, green, blue, white");
    gpio_set_level(DISPLAY_BACKLIGHT_GPIO, DISPLAY_BACKLIGHT_OUTPUT_INVERT ? 0 : 1);
    constexpr uint16_t kColors[] = {0xF800, 0x07E0, 0x001F, 0xFFFF};
    for (uint16_t color : kColors) {
        if (!DrawSolidColor(color)) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(350));
    }
#endif
}

bool FujiDisplay::InitializeSt7735() {
    gpio_config_t backlight_config = {};
    backlight_config.pin_bit_mask = 1ULL << DISPLAY_BACKLIGHT_GPIO;
    backlight_config.mode = GPIO_MODE_OUTPUT;
    if (!CheckStep(gpio_config(&backlight_config), "backlight GPIO setup")) {
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
    if (!CheckStep(
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
    if (!CheckStep(
            esp_lcd_new_panel_io_spi(DISPLAY_SPI_HOST, &io_config, &panel_io_),
            "panel IO setup")) {
        return false;
    }

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = DISPLAY_RESET_GPIO;
    panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
    panel_config.bits_per_pixel = 16;
    if (!CheckStep(
            esp_lcd_new_panel_st7735(panel_io_, &panel_config, &panel_),
            "ST7735 driver setup")) {
        return false;
    }

    if (!CheckStep(esp_lcd_panel_reset(panel_), "reset") ||
        !CheckStep(esp_lcd_panel_init(panel_), "initialization") ||
        !CheckStep(
            esp_lcd_panel_invert_color(panel_, DISPLAY_INVERT_COLOR),
            "color inversion") ||
        !CheckStep(
            esp_lcd_panel_swap_xy(panel_, DISPLAY_SWAP_XY),
            "axis configuration") ||
        !CheckStep(
            esp_lcd_panel_mirror(panel_, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y),
            "mirror configuration")) {
        return false;
    }

    const esp_err_t display_on_result = esp_lcd_panel_disp_on_off(panel_, true);
    if (display_on_result != ESP_OK && display_on_result != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGE(TAG, "display power-on failed: %s; continuing without a display",
                 esp_err_to_name(display_on_result));
        return false;
    }
    if (display_on_result == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "panel does not support display power control; assuming it is on");
    }

    ESP_LOGW(TAG, "ST7735 SPI initialized, but this write-only bus cannot verify panel presence");
    RunSt7735SelfTest();
#if CONFIG_BOARD_DISPLAY_TEST_ONLY
    ESP_LOGI(TAG, "display-only test is holding the final white frame");
    return true;
#else
    display_ = new SpiLcdDisplay(
        panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT,
        DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
        DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    return display_ != nullptr;
#endif
}
