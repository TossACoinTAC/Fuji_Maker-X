#ifndef FUJI_DISPLAY_H
#define FUJI_DISPLAY_H

#include "display/display.h"

#include <driver/i2c_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

#include <cstdint>

class FujiDisplay {
private:
    Display* display_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    i2c_master_bus_handle_t i2c_bus_ = nullptr;

    bool CheckStep(esp_err_t result, const char* step);
    int ScanOledAddress();
    bool DrawOledFill(uint8_t value, const char* step);
    bool DrawSolidColor(uint16_t rgb565);
    void RunSt7735SelfTest();

public:
    bool InitializeOled();
    bool InitializeSt7735();

    Display* GetDisplay() const { return display_; }
};

#endif  // FUJI_DISPLAY_H
