#pragma once

#include <driver/i2c_master.h>

#include <cstddef>
#include <cstdint>

struct WaveshareImuSample {
    float acceleration_g[3] = {};
    float angular_rate_dps[3] = {};
};

class WaveshareImu {
public:
    ~WaveshareImu();

    bool Initialize(i2c_master_bus_handle_t i2c_bus);
    bool ReadSample(WaveshareImuSample& sample);

    uint8_t address() const { return address_; }
    uint8_t revision() const { return revision_; }

private:
    bool AttachAtAddress(i2c_master_bus_handle_t i2c_bus, uint8_t address);
    bool ReadRegister(uint8_t reg, uint8_t* data, size_t length);
    bool WriteRegister(uint8_t reg, uint8_t value);

    i2c_master_dev_handle_t device_ = nullptr;
    uint8_t address_ = 0;
    uint8_t revision_ = 0;
};
