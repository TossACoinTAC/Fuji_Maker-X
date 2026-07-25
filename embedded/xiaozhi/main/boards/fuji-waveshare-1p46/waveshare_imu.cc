#include "waveshare_imu.h"

#include "config.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <array>
#include <cmath>

#define TAG "FujiWsImu"

namespace {

constexpr uint8_t kWhoAmIRegister = 0x00;
constexpr uint8_t kRevisionRegister = 0x01;
constexpr uint8_t kControl1Register = 0x02;
constexpr uint8_t kControl2Register = 0x03;
constexpr uint8_t kControl3Register = 0x04;
constexpr uint8_t kControl5Register = 0x06;
constexpr uint8_t kControl6Register = 0x07;
constexpr uint8_t kControl7Register = 0x08;
constexpr uint8_t kAccelerationXLowRegister = 0x35;
constexpr uint8_t kExpectedWhoAmI = 0x05;

constexpr uint8_t kAcceleration4gAt120Hz = 0x16;
constexpr uint8_t kGyroscope256dpsAt120Hz = 0x46;
constexpr uint8_t kAccelerationAndGyroscopeLpf = 0x55;
constexpr uint8_t kEnableAccelerationAndGyroscope = 0x43;

int16_t ReadLeInt16(const uint8_t* bytes) {
    return static_cast<int16_t>(static_cast<uint16_t>(bytes[0]) |
                                (static_cast<uint16_t>(bytes[1]) << 8));
}

}  // namespace

WaveshareImu::~WaveshareImu() {
    if (device_ != nullptr) {
        i2c_master_bus_rm_device(device_);
    }
}

bool WaveshareImu::AttachAtAddress(i2c_master_bus_handle_t i2c_bus, uint8_t address) {
    i2c_device_config_t device_config = {};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = address;
    device_config.scl_speed_hz = BOARD_I2C_CLOCK_HZ;
    const esp_err_t result = i2c_master_bus_add_device(i2c_bus, &device_config, &device_);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "could not attach I2C 0x%02X: %s", address, esp_err_to_name(result));
        return false;
    }

    uint8_t who_am_i = 0;
    if (!ReadRegister(kWhoAmIRegister, &who_am_i, sizeof(who_am_i)) ||
        who_am_i != kExpectedWhoAmI) {
        ESP_LOGW(TAG, "unexpected WHO_AM_I 0x%02X at I2C 0x%02X", who_am_i, address);
        i2c_master_bus_rm_device(device_);
        device_ = nullptr;
        return false;
    }
    address_ = address;
    return true;
}

bool WaveshareImu::Initialize(i2c_master_bus_handle_t i2c_bus) {
    if (!AttachAtAddress(i2c_bus, IMU_I2C_ADDRESS_HIGH) &&
        !AttachAtAddress(i2c_bus, IMU_I2C_ADDRESS_LOW)) {
        ESP_LOGE(TAG, "QMI8658 not found at either supported address");
        return false;
    }

    uint8_t control1 = 0;
    if (!ReadRegister(kRevisionRegister, &revision_, sizeof(revision_)) ||
        !ReadRegister(kControl1Register, &control1, sizeof(control1))) {
        return false;
    }

    control1 = static_cast<uint8_t>((control1 & ~0x01U) | 0x40U);
    if (!WriteRegister(kControl1Register, control1) ||
        !WriteRegister(kControl2Register, kAcceleration4gAt120Hz) ||
        !WriteRegister(kControl3Register, kGyroscope256dpsAt120Hz) ||
        !WriteRegister(kControl5Register, kAccelerationAndGyroscopeLpf) ||
        !WriteRegister(kControl6Register, 0x00) ||
        !WriteRegister(kControl7Register, kEnableAccelerationAndGyroscope)) {
        return false;
    }
    WaveshareImuSample sample;
    bool sample_ready = false;
    for (int attempt = 0; attempt < 10; ++attempt) {
        vTaskDelay(pdMS_TO_TICKS(20));
        if (!ReadSample(sample)) {
            return false;
        }
        const float acceleration_sum = std::fabs(sample.acceleration_g[0]) +
                                       std::fabs(sample.acceleration_g[1]) +
                                       std::fabs(sample.acceleration_g[2]);
        if (acceleration_sum > 0.1f) {
            sample_ready = true;
            break;
        }
    }
    if (!sample_ready) {
        ESP_LOGE(TAG, "QMI8658 did not produce a valid acceleration sample");
        return false;
    }
    ESP_LOGI(TAG,
             "QMI8658 ready at 0x%02X revision=0x%02X; accel[g]=%.3f,%.3f,%.3f "
             "gyro[dps]=%.2f,%.2f,%.2f",
             address_, revision_, sample.acceleration_g[0], sample.acceleration_g[1],
             sample.acceleration_g[2], sample.angular_rate_dps[0], sample.angular_rate_dps[1],
             sample.angular_rate_dps[2]);
    return true;
}

bool WaveshareImu::ReadRegister(uint8_t reg, uint8_t* data, size_t length) {
    const esp_err_t result =
        i2c_master_transmit_receive(device_, &reg, sizeof(reg), data, length, 100);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "read reg 0x%02X failed: %s", reg, esp_err_to_name(result));
        return false;
    }
    return true;
}

bool WaveshareImu::WriteRegister(uint8_t reg, uint8_t value) {
    const std::array<uint8_t, 2> payload = {reg, value};
    const esp_err_t result = i2c_master_transmit(device_, payload.data(), payload.size(), 100);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "write reg 0x%02X failed: %s", reg, esp_err_to_name(result));
        return false;
    }
    return true;
}

bool WaveshareImu::ReadSample(WaveshareImuSample& sample) {
    std::array<uint8_t, 12> data = {};
    if (!ReadRegister(kAccelerationXLowRegister, data.data(), data.size())) {
        return false;
    }
    constexpr float kAccelerationScale = 4.0f / 32768.0f;
    constexpr float kGyroscopeScale = 256.0f / 32768.0f;
    for (size_t axis = 0; axis < 3; ++axis) {
        sample.acceleration_g[axis] = ReadLeInt16(data.data() + axis * 2) * kAccelerationScale;
        sample.angular_rate_dps[axis] =
            ReadLeInt16(data.data() + 6 + axis * 2) * kGyroscopeScale;
    }
    return true;
}
