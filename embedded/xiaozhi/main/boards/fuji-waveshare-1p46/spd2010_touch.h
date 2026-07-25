#pragma once

#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <cstddef>
#include <cstdint>

class Spd2010Touch {
public:
    struct Point {
        uint16_t x = 0;
        uint16_t y = 0;
        uint8_t id = 0;
        uint8_t weight = 0;
    };

    bool Initialize(i2c_master_bus_handle_t i2c_bus);
    bool ReadPoint(Point& point);
    bool WaitForInterrupt(TickType_t timeout_ticks);
    bool interrupt_asserted() const;

private:
    struct Status {
        bool point_exists = false;
        bool gesture = false;
        bool auxiliary = false;
        bool in_bios = false;
        bool in_cpu = false;
        bool cpu_running = false;
        uint16_t read_length = 0;
    };

    static void InterruptHandler(void* argument);
    bool ReadRegister(uint16_t reg, uint8_t* data, size_t length);
    bool WriteRegister(uint16_t reg, const uint8_t* data, size_t length);
    bool WriteCommand(uint16_t reg, uint16_t value);
    bool ReadStatus(Status& status);
    bool ReadFirmwareVersion();
    bool ReadTouchPacket(const Status& status, Point& point);
    void FinishPacket();

    i2c_master_dev_handle_t device_ = nullptr;
    QueueHandle_t interrupt_queue_ = nullptr;
};
