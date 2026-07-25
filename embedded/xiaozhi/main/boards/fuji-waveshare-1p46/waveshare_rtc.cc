#include "waveshare_rtc.h"

#include "config.h"

#include <esp_log.h>

#include <array>

#define TAG "FujiWsRtc"

namespace {

constexpr uint8_t kControl1Register = 0x00;
constexpr uint8_t kSecondsRegister = 0x04;
constexpr uint8_t kControl1Stop = 0x20;
constexpr uint8_t kControl1TwelveHour = 0x02;
constexpr uint8_t kSecondsVoltageLow = 0x80;

uint8_t BcdToDecimal(uint8_t value) {
    return static_cast<uint8_t>((value >> 4) * 10 + (value & 0x0F));
}

bool IsValidBcd(uint8_t value) {
    return (value & 0x0F) <= 9 && ((value >> 4) & 0x0F) <= 9;
}

}  // namespace

WaveshareRtc::~WaveshareRtc() {
    if (device_ != nullptr) {
        i2c_master_bus_rm_device(device_);
    }
}

bool WaveshareRtc::Initialize(i2c_master_bus_handle_t i2c_bus) {
    i2c_device_config_t device_config = {};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = RTC_I2C_ADDRESS;
    device_config.scl_speed_hz = BOARD_I2C_CLOCK_HZ;
    const esp_err_t result = i2c_master_bus_add_device(i2c_bus, &device_config, &device_);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "PCF85063 I2C setup failed: %s", esp_err_to_name(result));
        return false;
    }

    WaveshareRtcDateTime date_time;
    if (!ReadDateTime(date_time)) {
        return false;
    }
    if (date_time.valid) {
        ESP_LOGI(TAG, "PCF85063 read-only time %04u-%02u-%02u wday=%u %02u:%02u:%02u",
                 date_time.year, date_time.month, date_time.day, date_time.weekday,
                 date_time.hour, date_time.minute, date_time.second);
    } else {
        ESP_LOGW(TAG, "PCF85063 responded but time is not valid (voltage_low=%d stopped=%d)",
                 date_time.voltage_low, date_time.stopped);
    }
    return true;
}

bool WaveshareRtc::ReadRegisters(uint8_t first_register, uint8_t* data, size_t length) {
    const esp_err_t result =
        i2c_master_transmit_receive(device_, &first_register, sizeof(first_register), data, length, 100);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "read reg 0x%02X failed: %s", first_register, esp_err_to_name(result));
        return false;
    }
    return true;
}

bool WaveshareRtc::ReadDateTime(WaveshareRtcDateTime& date_time) {
    std::array<uint8_t, 11> registers = {};
    if (!ReadRegisters(kControl1Register, registers.data(), registers.size())) {
        return false;
    }

    const uint8_t seconds = registers[kSecondsRegister];
    const uint8_t minutes = registers[0x05];
    const uint8_t hours = registers[0x06];
    const uint8_t day = registers[0x07];
    const uint8_t weekday = registers[0x08];
    const uint8_t month = registers[0x09];
    const uint8_t year = registers[0x0A];
    const bool twelve_hour_mode =
        (registers[kControl1Register] & kControl1TwelveHour) != 0;
    const uint8_t hour_bcd = hours & (twelve_hour_mode ? 0x1F : 0x3F);

    date_time.voltage_low = (seconds & kSecondsVoltageLow) != 0;
    date_time.stopped = (registers[kControl1Register] & kControl1Stop) != 0;
    date_time.second = BcdToDecimal(seconds & 0x7F);
    date_time.minute = BcdToDecimal(minutes & 0x7F);
    if (twelve_hour_mode) {
        const bool is_pm = (hours & 0x20) != 0;
        const uint8_t hour = BcdToDecimal(hour_bcd);
        date_time.hour = static_cast<uint8_t>((hour % 12) + (is_pm ? 12 : 0));
    } else {
        date_time.hour = BcdToDecimal(hour_bcd);
    }
    date_time.day = BcdToDecimal(day & 0x3F);
    date_time.weekday = weekday & 0x07;
    date_time.month = BcdToDecimal(month & 0x1F);
    date_time.year = static_cast<uint16_t>(2000 + BcdToDecimal(year));

    const bool bcd_valid = IsValidBcd(seconds & 0x7F) && IsValidBcd(minutes & 0x7F) &&
                           IsValidBcd(hour_bcd) && IsValidBcd(day & 0x3F) &&
                           IsValidBcd(month & 0x1F) && IsValidBcd(year);
    const bool ranges_valid = date_time.second < 60 && date_time.minute < 60 &&
                              date_time.hour < 24 && date_time.day >= 1 && date_time.day <= 31 &&
                              date_time.weekday <= 6 && date_time.month >= 1 && date_time.month <= 12;
    date_time.valid = bcd_valid && ranges_valid && !date_time.voltage_low && !date_time.stopped;
    return true;
}
