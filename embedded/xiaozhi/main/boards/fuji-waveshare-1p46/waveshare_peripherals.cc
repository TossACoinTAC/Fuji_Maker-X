#include "waveshare_peripherals.h"

#include "config.h"

#include <driver/gpio.h>
#include <esp_io_expander_tca9554.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <array>

#define TAG "FujiWsPeriph"

namespace {

struct ExpectedI2cDevice {
    uint8_t address;
    const char* name;
};

constexpr std::array<ExpectedI2cDevice, 5> kExpectedDevices = {{
    {TCA9554_I2C_ADDRESS, "TCA9554"},
    {RTC_I2C_ADDRESS, "PCF85063"},
    {TOUCH_I2C_ADDRESS, "SPD2010 touch"},
    {IMU_I2C_ADDRESS_LOW, "QMI8658 (low address)"},
    {IMU_I2C_ADDRESS_HIGH, "QMI8658 (high address)"},
}};

}  // namespace

void InitializeWavesharePowerHold() {
    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << POWER_HOLD_GPIO;
    config.mode = GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&config));
    ESP_ERROR_CHECK(gpio_set_level(POWER_HOLD_GPIO, 1));
    ESP_LOGI(TAG, "power hold asserted on GPIO%d; battery path remains untested", POWER_HOLD_GPIO);
}

void ReleaseWavesharePowerHold() {
    ESP_LOGW(TAG, "releasing power hold on GPIO%d", POWER_HOLD_GPIO);
    ESP_ERROR_CHECK(gpio_set_level(POWER_HOLD_GPIO, 0));
}

bool WavesharePeripherals::CheckStep(esp_err_t result, const char* step) {
    if (result == ESP_OK) {
        return true;
    }
    ESP_LOGE(TAG, "%s failed: %s", step, esp_err_to_name(result));
    return false;
}

bool WavesharePeripherals::ScanI2cBus() {
    std::array<bool, 0x78> found = {};
    int device_count = 0;
    ESP_LOGI(TAG, "I2C scan: port=%d SDA=%d SCL=%d", BOARD_I2C_PORT, BOARD_I2C_SDA_GPIO,
             BOARD_I2C_SCL_GPIO);
    for (uint8_t address = 0x08; address <= 0x77; ++address) {
        if (i2c_master_probe(i2c_bus_, address, 50) != ESP_OK) {
            continue;
        }
        found[address] = true;
        ++device_count;
        ESP_LOGI(TAG, "I2C device found at 0x%02X", address);
    }

    for (const auto& expected : kExpectedDevices) {
        if (found[expected.address]) {
            ESP_LOGI(TAG, "%s responded at 0x%02X", expected.name, expected.address);
        }
    }
    if (!found[TCA9554_I2C_ADDRESS]) {
        ESP_LOGE(TAG, "required TCA9554 missing at 0x%02X", TCA9554_I2C_ADDRESS);
        return false;
    }
    if (!found[TOUCH_I2C_ADDRESS]) {
        ESP_LOGW(TAG,
                 "SPD2010 touch did not respond before panel reset; touch initialization will "
                 "retry after the display reset");
    }
    if (!found[RTC_I2C_ADDRESS]) {
        ESP_LOGW(TAG, "PCF85063 did not respond at 0x%02X", RTC_I2C_ADDRESS);
    }
    if (!found[IMU_I2C_ADDRESS_LOW] && !found[IMU_I2C_ADDRESS_HIGH]) {
        ESP_LOGW(TAG, "QMI8658 did not respond at 0x%02X or 0x%02X", IMU_I2C_ADDRESS_LOW,
                 IMU_I2C_ADDRESS_HIGH);
    }
    return device_count > 0;
}

bool WavesharePeripherals::Initialize() {
    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = BOARD_I2C_PORT;
    bus_config.sda_io_num = BOARD_I2C_SDA_GPIO;
    bus_config.scl_io_num = BOARD_I2C_SCL_GPIO;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;
    if (!CheckStep(i2c_new_master_bus(&bus_config, &i2c_bus_), "I2C bus setup")) {
        return false;
    }
    if (!CheckStep(esp_io_expander_new_i2c_tca9554(i2c_bus_, TCA9554_I2C_ADDRESS, &io_expander_),
                   "TCA9554 setup")) {
        return false;
    }

    const uint32_t reset_pins = TOUCH_RESET_EXPANDER_PIN | DISPLAY_RESET_EXPANDER_PIN;
    if (!CheckStep(esp_io_expander_set_dir(io_expander_, reset_pins, IO_EXPANDER_OUTPUT),
                   "TCA9554 reset pin direction") ||
        !CheckStep(esp_io_expander_set_level(io_expander_, reset_pins, 1),
                   "TCA9554 reset pin idle level")) {
        return false;
    }

    const uint32_t imu_interrupt_pins =
        IMU_INTERRUPT_1_EXPANDER_PIN | IMU_INTERRUPT_2_EXPANDER_PIN;
    if (!CheckStep(esp_io_expander_set_dir(io_expander_, imu_interrupt_pins, IO_EXPANDER_INPUT),
                   "QMI8658 interrupt pin direction")) {
        return false;
    }

    gpio_config_t rtc_interrupt_config = {};
    rtc_interrupt_config.pin_bit_mask = 1ULL << RTC_INTERRUPT_GPIO;
    rtc_interrupt_config.mode = GPIO_MODE_INPUT;
    rtc_interrupt_config.pull_up_en = GPIO_PULLUP_ENABLE;
    rtc_interrupt_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    rtc_interrupt_config.intr_type = GPIO_INTR_DISABLE;
    if (!CheckStep(gpio_config(&rtc_interrupt_config), "PCF85063 interrupt GPIO setup")) {
        return false;
    }

    // The touch controller is not guaranteed to acknowledge I2C until its
    // dedicated expander reset has completed after a cold boot.
    return ResetTouch() && ScanI2cBus();
}

bool WavesharePeripherals::ResetExpanderLine(uint32_t pin_mask, const char* name) {
    if (io_expander_ == nullptr) {
        ESP_LOGE(TAG, "%s reset requested before TCA9554 initialization", name);
        return false;
    }
    if (!CheckStep(esp_io_expander_set_level(io_expander_, pin_mask, 0), name)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    if (!CheckStep(esp_io_expander_set_level(io_expander_, pin_mask, 1), name)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "%s reset completed", name);
    return true;
}

bool WavesharePeripherals::ResetDisplay() {
    return ResetExpanderLine(DISPLAY_RESET_EXPANDER_PIN, "SPD2010 display");
}

bool WavesharePeripherals::ResetTouch() {
    return ResetExpanderLine(TOUCH_RESET_EXPANDER_PIN, "SPD2010 touch");
}
