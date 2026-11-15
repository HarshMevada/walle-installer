#pragma once

#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>

// TOF10120 laser distance sensor on the shared OLED I2C bus (address 0x52).
// A background task reads it ~15 times per second; distance_mm() is lock-free.
class ObstacleSensor {
public:
    bool Initialize(i2c_master_bus_handle_t bus);

    bool present() const { return present_; }
    // Latest distance in mm, or -1 when the sensor is missing / not ready.
    // Readings beyond the sensor's range are reported as kFarMm.
    int distance_mm() const { return distance_mm_.load(); }

    static constexpr int kFarMm = 2000;

private:
    i2c_master_dev_handle_t dev_ = nullptr;
    bool present_ = false;
    std::atomic<int> distance_mm_{-1};

    static void TaskEntry(void* arg);
    void TaskLoop();
    int ReadOnce();
};
