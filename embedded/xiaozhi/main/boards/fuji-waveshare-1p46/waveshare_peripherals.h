#pragma once

#include <driver/i2c_master.h>
#include <esp_io_expander.h>

void InitializeWavesharePowerHold();
void ReleaseWavesharePowerHold();

class WavesharePeripherals {
public:
    bool Initialize();
    bool ResetDisplay();
    bool ResetTouch();

    i2c_master_bus_handle_t i2c_bus() const { return i2c_bus_; }

private:
    bool CheckStep(esp_err_t result, const char* step);
    bool ScanI2cBus();
    bool ResetExpanderLine(uint32_t pin_mask, const char* name);

    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    esp_io_expander_handle_t io_expander_ = nullptr;
};
