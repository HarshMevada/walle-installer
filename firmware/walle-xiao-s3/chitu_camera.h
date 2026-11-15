#pragma once

#include "camera.h"

#include <esp_camera.h>

#include <string>

// Camera for Chitu: the sensor produces JPEG directly, so AI photos are uploaded
// as-is (xiaozhi's Esp32Camera tries to re-encode JPEG input and fails).
// Each photo is copied out of the camera's frame buffer straight away, so taking
// a photo never blocks the live phone stream.
class ChituCamera : public Camera {
public:
    explicit ChituCamera(const camera_config_t& config);
    ~ChituCamera();

    void SetExplainUrl(const std::string& url, const std::string& token) override;
    bool Capture() override;
    bool SetHMirror(bool enabled) override;
    bool SetVFlip(bool enabled) override;
    std::expected<std::string, std::string> Explain(const std::string& question) override;
    std::expected<std::string, std::string> Explain(const std::string& question,
                                                    const std::string& url,
                                                    const std::string& token) override;

private:
    bool ready_ = false;
    std::string explain_url_;
    std::string explain_token_;
    uint8_t* photo_ = nullptr;  // last captured JPEG (PSRAM)
    size_t photo_len_ = 0;
    size_t photo_cap_ = 0;
    int width_ = 0;
    int height_ = 0;
};
