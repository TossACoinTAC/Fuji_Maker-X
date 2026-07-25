#pragma once

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

class Backlight;
class Display;
class Spd2010Touch;
class WavesharePeripherals;

class WaveshareDisplay {
public:
    bool Initialize(WavesharePeripherals& peripherals, Spd2010Touch& touch);
    bool RunDisplayAndTouchTest(Backlight* backlight);
    void SetupDiagnosticUi(const char* status, const char* message);

    Display* GetDisplay() const { return display_; }

private:
    static void TouchTestTask(void* argument);
    bool CheckStep(esp_err_t result, const char* step);
    bool DrawSolidColor(uint16_t color);
    bool DrawRoundTestPattern();
    void DrawTouchMarker(uint16_t x, uint16_t y);

    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;
    Spd2010Touch* touch_ = nullptr;
    uint8_t touch_coverage_ = 0;
};
