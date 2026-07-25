#include "spd2010_touch.h"

#include "config.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_rom_sys.h>

#include <algorithm>
#include <array>
#include <cstring>

#define TAG "FujiWsTouch"

namespace {

constexpr uint16_t kStatusRegister = 0x2000;
constexpr uint16_t kTouchDataRegister = 0x0003;
constexpr uint16_t kHdpStatusRegister = 0xFC02;
constexpr uint16_t kFirmwareVersionRegister = 0x2600;
constexpr uint16_t kPointModeCommand = 0x5000;
constexpr uint16_t kTouchStartCommand = 0x4600;
constexpr uint16_t kCpuStartCommand = 0x0400;
constexpr uint16_t kClearInterruptCommand = 0x0200;
constexpr size_t kMaximumPacketLength = 64;

}  // namespace

void Spd2010Touch::InterruptHandler(void* argument) {
    auto* self = static_cast<Spd2010Touch*>(argument);
    const uint8_t event = 1;
    BaseType_t task_woken = pdFALSE;
    xQueueSendFromISR(self->interrupt_queue_, &event, &task_woken);
    if (task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

bool Spd2010Touch::Initialize(i2c_master_bus_handle_t i2c_bus) {
    i2c_device_config_t device_config = {};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = TOUCH_I2C_ADDRESS;
    device_config.scl_speed_hz = BOARD_I2C_CLOCK_HZ;
    esp_err_t result = i2c_master_bus_add_device(i2c_bus, &device_config, &device_);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "touch I2C device setup failed: %s", esp_err_to_name(result));
        return false;
    }

    gpio_config_t interrupt_config = {};
    interrupt_config.pin_bit_mask = 1ULL << TOUCH_INTERRUPT_GPIO;
    interrupt_config.mode = GPIO_MODE_INPUT;
    interrupt_config.pull_up_en = GPIO_PULLUP_ENABLE;
    interrupt_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    interrupt_config.intr_type = GPIO_INTR_NEGEDGE;
    result = gpio_config(&interrupt_config);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "touch interrupt GPIO setup failed: %s", esp_err_to_name(result));
        return false;
    }

    interrupt_queue_ = xQueueCreate(8, sizeof(uint8_t));
    if (interrupt_queue_ == nullptr) {
        ESP_LOGE(TAG, "touch interrupt queue allocation failed");
        return false;
    }
    result = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "GPIO ISR service setup failed: %s", esp_err_to_name(result));
        return false;
    }
    result = gpio_isr_handler_add(TOUCH_INTERRUPT_GPIO, InterruptHandler, this);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "touch interrupt handler setup failed: %s", esp_err_to_name(result));
        return false;
    }

    if (!ReadFirmwareVersion()) {
        return false;
    }
    ESP_LOGI(TAG, "SPD2010 touch initialized at I2C 0x%02X, INT=GPIO%d", TOUCH_I2C_ADDRESS,
             TOUCH_INTERRUPT_GPIO);
    return true;
}

bool Spd2010Touch::ReadRegister(uint16_t reg, uint8_t* data, size_t length) {
    const std::array<uint8_t, 2> address = {
        static_cast<uint8_t>(reg >> 8),
        static_cast<uint8_t>(reg & 0xFF),
    };
    const esp_err_t result =
        i2c_master_transmit_receive(device_, address.data(), address.size(), data, length, 1000);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "read reg 0x%04X failed: %s", reg, esp_err_to_name(result));
        return false;
    }
    return true;
}

bool Spd2010Touch::WriteRegister(uint16_t reg, const uint8_t* data, size_t length) {
    if (length > 8) {
        ESP_LOGE(TAG, "write reg 0x%04X rejected: %zu-byte payload", reg, length);
        return false;
    }
    std::array<uint8_t, 10> buffer = {};
    buffer[0] = static_cast<uint8_t>(reg >> 8);
    buffer[1] = static_cast<uint8_t>(reg & 0xFF);
    std::memcpy(buffer.data() + 2, data, length);
    const esp_err_t result = i2c_master_transmit(device_, buffer.data(), length + 2, 1000);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "write reg 0x%04X failed: %s", reg, esp_err_to_name(result));
        return false;
    }
    esp_rom_delay_us(200);
    return true;
}

bool Spd2010Touch::WriteCommand(uint16_t reg, uint16_t value) {
    const std::array<uint8_t, 2> data = {
        static_cast<uint8_t>(value & 0xFF),
        static_cast<uint8_t>(value >> 8),
    };
    return WriteRegister(reg, data.data(), data.size());
}

bool Spd2010Touch::ReadFirmwareVersion() {
    std::array<uint8_t, 18> data = {};
    if (!ReadRegister(kFirmwareVersionRegister, data.data(), data.size())) {
        return false;
    }
    const uint16_t version = static_cast<uint16_t>(data[4] | (data[5] << 8));
    const uint32_t product_id =
        static_cast<uint32_t>(data[6]) | (static_cast<uint32_t>(data[7]) << 8) |
        (static_cast<uint32_t>(data[8]) << 16) | (static_cast<uint32_t>(data[9]) << 24);
    ESP_LOGI(TAG,
             "firmware version=%u product_id=0x%08lX name_bytes=%02X%02X%02X%02X-%02X%02X%02X%02X",
             version, static_cast<unsigned long>(product_id), data[17], data[16], data[15],
             data[14], data[13], data[12], data[11], data[10]);
    return true;
}

bool Spd2010Touch::ReadStatus(Status& status) {
    std::array<uint8_t, 4> data = {};
    if (!ReadRegister(kStatusRegister, data.data(), data.size())) {
        return false;
    }
    status.point_exists = (data[0] & 0x01U) != 0;
    status.gesture = (data[0] & 0x02U) != 0;
    status.auxiliary = (data[0] & 0x08U) != 0;
    status.in_bios = (data[1] & 0x40U) != 0;
    status.in_cpu = (data[1] & 0x20U) != 0;
    status.cpu_running = (data[1] & 0x08U) != 0;
    status.read_length = static_cast<uint16_t>(data[2] | (data[3] << 8));
    return true;
}

bool Spd2010Touch::ReadTouchPacket(const Status& status, Point& point) {
    if (status.read_length < 10 || status.read_length > kMaximumPacketLength) {
        ESP_LOGW(TAG, "invalid touch packet length: %u", status.read_length);
        return false;
    }
    std::array<uint8_t, kMaximumPacketLength> data = {};
    if (!ReadRegister(kTouchDataRegister, data.data(), status.read_length)) {
        return false;
    }

    const size_t point_count = std::min<size_t>((status.read_length - 4) / 6, TOUCH_MAX_POINTS);
    if (point_count == 0 || data[4] > 0x0A) {
        return false;
    }
    point.id = data[4];
    point.x = static_cast<uint16_t>(((data[7] & 0xF0U) << 4) | data[5]);
    point.y = static_cast<uint16_t>(((data[7] & 0x0FU) << 8) | data[6]);
    point.weight = data[8];
    if (point.x >= DISPLAY_WIDTH || point.y >= DISPLAY_HEIGHT) {
        ESP_LOGW(TAG, "out-of-range touch point: x=%u y=%u", point.x, point.y);
        return false;
    }
    return true;
}

void Spd2010Touch::FinishPacket() {
    std::array<uint8_t, 8> status = {};
    for (int packet = 0; packet < 4; ++packet) {
        if (!ReadRegister(kHdpStatusRegister, status.data(), status.size())) {
            break;
        }
        const uint8_t state = status[5];
        const uint16_t next_length = static_cast<uint16_t>(status[2] | (status[3] << 8));
        if (state == 0x82) {
            WriteCommand(kClearInterruptCommand, 1);
            return;
        }
        if (state != 0x00 || next_length == 0 || next_length > kMaximumPacketLength) {
            break;
        }
        std::array<uint8_t, kMaximumPacketLength> remainder = {};
        if (!ReadRegister(kTouchDataRegister, remainder.data(), next_length)) {
            break;
        }
    }
    WriteCommand(kClearInterruptCommand, 1);
}

bool Spd2010Touch::ReadPoint(Point& point) {
    Status status;
    if (!ReadStatus(status)) {
        return false;
    }
    if (status.in_bios) {
        WriteCommand(kClearInterruptCommand, 1);
        WriteCommand(kCpuStartCommand, 1);
        return false;
    }
    if (status.in_cpu) {
        WriteCommand(kPointModeCommand, 0);
        WriteCommand(kTouchStartCommand, 0);
        WriteCommand(kClearInterruptCommand, 1);
        return false;
    }
    if (status.cpu_running && status.read_length == 0) {
        WriteCommand(kClearInterruptCommand, 1);
        return false;
    }
    if (!status.point_exists && !status.gesture) {
        if (status.cpu_running && status.auxiliary) {
            WriteCommand(kClearInterruptCommand, 1);
        }
        return false;
    }

    const bool has_point = status.point_exists && ReadTouchPacket(status, point);
    FinishPacket();
    return has_point;
}

bool Spd2010Touch::WaitForInterrupt(TickType_t timeout_ticks) {
    if (interrupt_queue_ == nullptr) {
        return false;
    }
    uint8_t event = 0;
    return xQueueReceive(interrupt_queue_, &event, timeout_ticks) == pdTRUE;
}

bool Spd2010Touch::interrupt_asserted() const { return gpio_get_level(TOUCH_INTERRUPT_GPIO) == 0; }
