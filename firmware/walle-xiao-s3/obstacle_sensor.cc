#include "obstacle_sensor.h"

#include "config.h"

#include <esp_log.h>

#include <algorithm>

#define TAG "ObstacleSensor"

bool ObstacleSensor::Initialize(i2c_master_bus_handle_t bus) {
    if (bus == nullptr) {
        return false;
    }
    if (i2c_master_probe(bus, TOF_I2C_ADDR, 100) != ESP_OK) {
        ESP_LOGW(TAG, "No TOF10120 found at 0x%02X - obstacle detection and roaming disabled",
                 TOF_I2C_ADDR);
        return false;
    }
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = TOF_I2C_ADDR;
    cfg.scl_speed_hz = TOF_I2C_SPEED_HZ;
    if (i2c_master_bus_add_device(bus, &cfg, &dev_) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add TOF10120 to the I2C bus");
        return false;
    }
    present_ = true;
    xTaskCreate(TaskEntry, "tof10120", 3072, this, 4, nullptr);
    ESP_LOGI(TAG, "TOF10120 ready at 0x%02X", TOF_I2C_ADDR);
    return true;
}

int ObstacleSensor::ReadOnce() {
    uint8_t reg = 0x00;  // real-time distance register
    if (i2c_master_transmit(dev_, &reg, 1, 50) != ESP_OK) {
        return -1;
    }
    vTaskDelay(pdMS_TO_TICKS(2));
    uint8_t buf[2] = {0, 0};
    if (i2c_master_receive(dev_, buf, 2, 50) != ESP_OK) {
        return -1;
    }
    int mm = (buf[0] << 8) | buf[1];
    if (mm <= 0 || mm > kFarMm) {
        mm = kFarMm;  // out of range = nothing close
    }
    return mm;
}

void ObstacleSensor::TaskEntry(void* arg) {
    static_cast<ObstacleSensor*>(arg)->TaskLoop();
}

void ObstacleSensor::TaskLoop() {
    int hist[3] = {kFarMm, kFarMm, kFarMm};
    int idx = 0;
    int failures = 0;
    while (true) {
        int mm = ReadOnce();
        if (mm < 0) {
            if (++failures >= 10) {
                distance_mm_ = -1;  // sensor stopped answering
            }
        } else {
            failures = 0;
            hist[idx] = mm;
            idx = (idx + 1) % 3;
            // median of the last 3 readings filters single spikes
            int a = hist[0], b = hist[1], c = hist[2];
            distance_mm_ = std::max(std::min(a, b), std::min(std::max(a, b), c));
        }
        vTaskDelay(pdMS_TO_TICKS(60));
    }
}
