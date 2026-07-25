#include "waveshare_display.h"

#include "backlight.h"
#include "config.h"
#include "display/lcd_display.h"
#include "spd2010_touch.h"
#include "waveshare_peripherals.h"

#include <driver/spi_master.h>
#include <esp_lcd_spd2010.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>

#include <algorithm>
#include <array>
#include <cmath>

#define TAG "FujiWsDisplay"

namespace {

class RoundLcdDisplay : public SpiLcdDisplay {
public:
    RoundLcdDisplay(esp_lcd_panel_io_handle_t io_handle, esp_lcd_panel_handle_t panel_handle,
                    Spd2010Touch* touch)
        : SpiLcdDisplay(io_handle, panel_handle, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X,
                        DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY),
          touch_(touch) {}

    void SetupUI() override {
        SpiLcdDisplay::SetupUI();
        DisplayLockGuard lock(this);
        lv_display_add_event_cb(display_, AlignInvalidatedArea, LV_EVENT_INVALIDATE_AREA, nullptr);
        lv_indev_t* input = lv_indev_create();
        lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(input, ReadTouchInput);
        lv_indev_set_user_data(input, touch_);
        lv_indev_set_display(input, display_);
    }

private:
    static void AlignInvalidatedArea(lv_event_t* event) {
        auto* area = static_cast<lv_area_t*>(lv_event_get_param(event));
        area->x1 = (area->x1 >> 2) << 2;
        area->x2 = ((area->x2 >> 2) << 2) + 3;
        area->x2 = std::min<int32_t>(area->x2, DISPLAY_WIDTH - 1);
    }

    static void ReadTouchInput(lv_indev_t* input, lv_indev_data_t* data) {
        auto* touch = static_cast<Spd2010Touch*>(lv_indev_get_user_data(input));
        data->state = LV_INDEV_STATE_RELEASED;
        if (touch == nullptr || !touch->interrupt_asserted()) {
            return;
        }
        Spd2010Touch::Point point;
        if (!touch->ReadPoint(point)) {
            return;
        }
        data->point.x = point.x;
        data->point.y = point.y;
        data->state = LV_INDEV_STATE_PRESSED;
    }

    Spd2010Touch* touch_;
};

constexpr uint16_t WireRgb565(uint16_t color) {
    return static_cast<uint16_t>((color << 8) | (color >> 8));
}

constexpr uint16_t kBlack = WireRgb565(0x0000);
constexpr uint16_t kWhite = WireRgb565(0xFFFF);
constexpr uint16_t kRed = WireRgb565(0xF800);
constexpr uint16_t kGreen = WireRgb565(0x07E0);
constexpr uint16_t kBlue = WireRgb565(0x001F);
constexpr uint16_t kDarkGray = WireRgb565(0x18E3);
constexpr uint16_t kYellow = WireRgb565(0xFFE0);

}  // namespace

bool WaveshareDisplay::CheckStep(esp_err_t result, const char* step) {
    if (result == ESP_OK) {
        return true;
    }
    ESP_LOGE(TAG, "%s failed: %s", step, esp_err_to_name(result));
    return false;
}

bool WaveshareDisplay::Initialize(WavesharePeripherals& peripherals, Spd2010Touch& touch) {
    if (!peripherals.ResetDisplay()) {
        return false;
    }

    spi_bus_config_t bus_config = {};
    bus_config.data0_io_num = DISPLAY_SPI_DATA0_GPIO;
    bus_config.data1_io_num = DISPLAY_SPI_DATA1_GPIO;
    bus_config.sclk_io_num = DISPLAY_SPI_SCLK_GPIO;
    bus_config.data2_io_num = DISPLAY_SPI_DATA2_GPIO;
    bus_config.data3_io_num = DISPLAY_SPI_DATA3_GPIO;
    bus_config.max_transfer_sz = DISPLAY_WIDTH * 80 * sizeof(uint16_t);
    if (!CheckStep(spi_bus_initialize(DISPLAY_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO),
                   "QSPI bus setup")) {
        return false;
    }

    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = DISPLAY_SPI_CS_GPIO;
    io_config.dc_gpio_num = GPIO_NUM_NC;
    io_config.spi_mode = DISPLAY_SPI_MODE;
    io_config.pclk_hz = DISPLAY_SPI_CLOCK_HZ;
    io_config.trans_queue_depth = 10;
    io_config.lcd_cmd_bits = 32;
    io_config.lcd_param_bits = 8;
    io_config.flags.quad_mode = true;
    if (!CheckStep(esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(DISPLAY_SPI_HOST),
                                            &io_config, &panel_io_),
                   "SPD2010 panel IO setup")) {
        return false;
    }

    spd2010_vendor_config_t vendor_config = {};
    vendor_config.flags.use_qspi_interface = 1;
    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = GPIO_NUM_NC;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = DISPLAY_BITS_PER_PIXEL;
    panel_config.vendor_config = &vendor_config;
    if (!CheckStep(esp_lcd_new_panel_spd2010(panel_io_, &panel_config, &panel_),
                   "SPD2010 panel setup") ||
        !CheckStep(esp_lcd_panel_reset(panel_), "SPD2010 software reset") ||
        !CheckStep(esp_lcd_panel_init(panel_), "SPD2010 initialization") ||
        !CheckStep(esp_lcd_panel_disp_on_off(panel_, true), "SPD2010 display enable")) {
        return false;
    }
    if (DISPLAY_SWAP_XY && !CheckStep(esp_lcd_panel_swap_xy(panel_, true), "SPD2010 axis setup")) {
        return false;
    }
    if ((DISPLAY_MIRROR_X || DISPLAY_MIRROR_Y) &&
        !CheckStep(esp_lcd_panel_mirror(panel_, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y),
                   "SPD2010 mirror setup")) {
        return false;
    }

    if (peripherals.ResetTouch() && touch.Initialize(peripherals.i2c_bus())) {
        touch_ = &touch;
    } else {
        ESP_LOGW(TAG, "touch initialization failed; LCD diagnostics remain available");
    }
    display_ = new RoundLcdDisplay(panel_io_, panel_, touch_);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "round LCD display allocation failed");
        return false;
    }
    ESP_LOGI(TAG, "SPD2010 display initialized: %dx%d QSPI=%dMHz mode=%d", DISPLAY_WIDTH,
             DISPLAY_HEIGHT, DISPLAY_SPI_CLOCK_HZ / 1000000, DISPLAY_SPI_MODE);
    return true;
}

bool WaveshareDisplay::DrawSolidColor(uint16_t color) {
    std::array<uint16_t, DISPLAY_WIDTH> line;
    line.fill(color);
    for (int y = 0; y < DISPLAY_HEIGHT; ++y) {
        if (!CheckStep(esp_lcd_panel_draw_bitmap(panel_, 0, y, DISPLAY_WIDTH, y + 1, line.data()),
                       "solid color draw")) {
            return false;
        }
    }
    return true;
}

bool WaveshareDisplay::DrawRoundTestPattern() {
    std::array<uint16_t, DISPLAY_WIDTH> line;
    constexpr int kCenter = DISPLAY_WIDTH / 2;
    constexpr int kRadius = DISPLAY_WIDTH / 2 - 2;
    constexpr int kRadiusSquared = kRadius * kRadius;
    for (int y = 0; y < DISPLAY_HEIGHT; ++y) {
        const int dy = y - kCenter;
        for (int x = 0; x < DISPLAY_WIDTH; ++x) {
            const int dx = x - kCenter;
            const int distance_squared = dx * dx + dy * dy;
            uint16_t color = distance_squared <= kRadiusSquared ? kDarkGray : kBlack;
            if (std::abs(distance_squared - kRadiusSquared) < 600) {
                color = kWhite;
            }
            if ((std::abs(dx) <= 1 || std::abs(dy) <= 1) && distance_squared < kRadiusSquared) {
                color = kGreen;
            }
            line[x] = color;
        }
        if (!CheckStep(esp_lcd_panel_draw_bitmap(panel_, 0, y, DISPLAY_WIDTH, y + 1, line.data()),
                       "round test pattern draw")) {
            return false;
        }
    }
    return true;
}

void WaveshareDisplay::DrawTouchMarker(uint16_t x, uint16_t y) {
    constexpr int kMarkerRadius = 5;
    std::array<uint16_t, kMarkerRadius * 2 + 1> horizontal;
    horizontal.fill(kYellow);
    const int x_start = std::max(0, static_cast<int>(x) - kMarkerRadius);
    const int x_end = std::min(DISPLAY_WIDTH, static_cast<int>(x) + kMarkerRadius + 1);
    const int y_start = std::max(0, static_cast<int>(y) - kMarkerRadius);
    const int y_end = std::min(DISPLAY_HEIGHT, static_cast<int>(y) + kMarkerRadius + 1);
    for (int marker_y = y_start; marker_y < y_end; ++marker_y) {
        esp_lcd_panel_draw_bitmap(panel_, x_start, marker_y, x_end, marker_y + 1,
                                  horizontal.data());
    }
}

void WaveshareDisplay::TouchTestTask(void* argument) {
    auto* self = static_cast<WaveshareDisplay*>(argument);
    while (true) {
        const bool notified = self->touch_->WaitForInterrupt(pdMS_TO_TICKS(100));
        if (!notified && !self->touch_->interrupt_asserted()) {
            continue;
        }
        Spd2010Touch::Point point;
        if (!self->touch_->ReadPoint(point)) {
            continue;
        }

        constexpr int kEdgeZone = 60;
        constexpr int kCenterZone = 55;
        if (point.x < kEdgeZone)
            self->touch_coverage_ |= 1U << 0;
        if (point.x >= DISPLAY_WIDTH - kEdgeZone)
            self->touch_coverage_ |= 1U << 1;
        if (point.y < kEdgeZone)
            self->touch_coverage_ |= 1U << 2;
        if (point.y >= DISPLAY_HEIGHT - kEdgeZone)
            self->touch_coverage_ |= 1U << 3;
        if (std::abs(static_cast<int>(point.x) - DISPLAY_WIDTH / 2) < kCenterZone &&
            std::abs(static_cast<int>(point.y) - DISPLAY_HEIGHT / 2) < kCenterZone) {
            self->touch_coverage_ |= 1U << 4;
        }
        self->DrawTouchMarker(point.x, point.y);
        ESP_LOGI(TAG, "touch id=%u x=%u y=%u weight=%u coverage=0x%02X", point.id, point.x, point.y,
                 point.weight, self->touch_coverage_);
    }
}

bool WaveshareDisplay::RunDisplayAndTouchTest(Backlight* backlight) {
    if (backlight == nullptr || panel_ == nullptr) {
        return false;
    }
    backlight->SetBrightness(30);
    vTaskDelay(pdMS_TO_TICKS(220));
    ESP_LOGI(TAG, "display self-test: red, green, blue, white, black, round boundary");
    constexpr std::array<uint16_t, 5> colors = {
        kRed, kGreen, kBlue, kWhite, kBlack,
    };
    for (uint16_t color : colors) {
        if (!DrawSolidColor(color)) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(350));
    }
    if (!DrawRoundTestPattern()) {
        return false;
    }
    if (touch_ == nullptr) {
        ESP_LOGE(TAG, "LCD sequence passed, but touch is unavailable");
        return false;
    }
    if (xTaskCreate(TouchTestTask, "fuji_touch_test", 4096, this, 4, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "touch test task creation failed");
        return false;
    }
    ESP_LOGI(TAG, "touch test ready; touch center and four edges, then slide continuously");
    return true;
}

void WaveshareDisplay::SetupDiagnosticUi(const char* status, const char* message) {
    if (display_ == nullptr) {
        return;
    }
    display_->SetupUI();
    display_->SetStatus(status);
    display_->SetChatMessage("system", message);
}
