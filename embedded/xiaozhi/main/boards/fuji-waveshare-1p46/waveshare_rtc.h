#pragma once

#include <driver/i2c_master.h>

#include <cstddef>
#include <cstdint>

struct WaveshareRtcDateTime {
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    uint8_t weekday = 0;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    bool voltage_low = false;
    bool stopped = false;
    bool valid = false;
};

class WaveshareRtc {
public:
    ~WaveshareRtc();

    bool Initialize(i2c_master_bus_handle_t i2c_bus);
    bool ReadDateTime(WaveshareRtcDateTime& date_time);

private:
    bool ReadRegisters(uint8_t first_register, uint8_t* data, size_t length);

    i2c_master_dev_handle_t device_ = nullptr;
};
