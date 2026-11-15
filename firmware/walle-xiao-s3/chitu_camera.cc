#include "chitu_camera.h"

#include "board.h"
#include "system_info.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

#define TAG "ChituCamera"

ChituCamera::ChituCamera(const camera_config_t& config) {
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_camera_init failed with error 0x%x", err);
        return;
    }
    sensor_t* s = esp_camera_sensor_get();
    if (s != nullptr) {
#if CONFIG_XIAOZHI_CAMERA_MIRROR_CONFIGURED
#if CONFIG_XIAOZHI_CAMERA_HMIRROR
        s->set_hmirror(s, 1);
#else
        s->set_hmirror(s, 0);
#endif
#if CONFIG_XIAOZHI_CAMERA_VFLIP
        s->set_vflip(s, 1);
#else
        s->set_vflip(s, 0);
#endif
#endif
        ESP_LOGI(TAG, "Camera ready (sensor PID 0x%x, JPEG)", s->id.PID);
    }
    ready_ = true;
}

ChituCamera::~ChituCamera() {
    if (photo_ != nullptr) {
        heap_caps_free(photo_);
    }
    if (ready_) {
        esp_camera_deinit();
    }
}

void ChituCamera::SetExplainUrl(const std::string& url, const std::string& token) {
    explain_url_ = url;
    explain_token_ = token;
}

bool ChituCamera::Capture() {
    if (!ready_) {
        return false;
    }
    // With CAMERA_GRAB_WHEN_EMPTY the first buffer can be old: take a fresh one
    camera_fb_t* fb = nullptr;
    for (int i = 0; i < 2; i++) {
        if (fb != nullptr) {
            esp_camera_fb_return(fb);
        }
        fb = esp_camera_fb_get();
        if (fb == nullptr) {
            ESP_LOGE(TAG, "Camera capture failed");
            return false;
        }
    }
    if (fb->format != PIXFORMAT_JPEG || fb->len == 0) {
        ESP_LOGE(TAG, "Unexpected frame format %d (expected JPEG)", fb->format);
        esp_camera_fb_return(fb);
        return false;
    }
    if (photo_cap_ < fb->len) {
        if (photo_ != nullptr) {
            heap_caps_free(photo_);
        }
        photo_cap_ = fb->len + 16 * 1024;  // headroom so later photos rarely reallocate
        photo_ = static_cast<uint8_t*>(heap_caps_malloc(photo_cap_, MALLOC_CAP_SPIRAM));
        if (photo_ == nullptr) {
            photo_cap_ = 0;
            photo_len_ = 0;
            esp_camera_fb_return(fb);
            ESP_LOGE(TAG, "No memory for photo");
            return false;
        }
    }
    memcpy(photo_, fb->buf, fb->len);
    photo_len_ = fb->len;
    width_ = fb->width;
    height_ = fb->height;
    esp_camera_fb_return(fb);  // give the buffer back to the camera / phone stream
    ESP_LOGI(TAG, "Captured %dx%d JPEG, %u bytes", width_, height_, (unsigned)photo_len_);
    return true;
}

bool ChituCamera::SetHMirror(bool enabled) {
    sensor_t* s = esp_camera_sensor_get();
    if (s == nullptr) {
        return false;
    }
    s->set_hmirror(s, enabled ? 1 : 0);
    return true;
}

bool ChituCamera::SetVFlip(bool enabled) {
    sensor_t* s = esp_camera_sensor_get();
    if (s == nullptr) {
        return false;
    }
    s->set_vflip(s, enabled ? 1 : 0);
    return true;
}

std::expected<std::string, std::string> ChituCamera::Explain(const std::string& question) {
    return Explain(question, explain_url_, explain_token_);
}

std::expected<std::string, std::string> ChituCamera::Explain(const std::string& question,
                                                             const std::string& url,
                                                             const std::string& token) {
    if (url.empty()) {
        return std::unexpected("Image explain URL or token is not set");
    }
    if (photo_ == nullptr || photo_len_ == 0) {
        return std::unexpected("No camera frame captured");
    }

    // Same upload format as xiaozhi's Esp32Camera: multipart with an explicit
    // Content-Length (the server rejects chunked uploads), JPEG sent as-is.
    const std::string boundary = "----ESP32_CAMERA_BOUNDARY";
    std::string head;
    head += "--" + boundary + "\r\n";
    head += "Content-Disposition: form-data; name=\"question\"\r\n\r\n";
    head += question + "\r\n";
    head += "--" + boundary + "\r\n";
    head += "Content-Disposition: form-data; name=\"file\"; filename=\"camera.jpg\"\r\n";
    head += "Content-Type: image/jpeg\r\n\r\n";
    const std::string tail = "\r\n--" + boundary + "--\r\n";

    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(3);
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());
    if (!token.empty()) {
        http->SetHeader("Authorization", "Bearer " + token);
    }
    http->SetHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    http->SetHeader("Content-Length", std::to_string(head.size() + photo_len_ + tail.size()));
    if (auto opened = http->Open("POST", url); !opened) {
        ESP_LOGE(TAG, "Failed to upload photo: %s", opened.error().ToString().c_str());
        http->Close();
        return std::unexpected("Failed to upload photo");
    }

    auto write_all = [&http](const void* data, size_t size) -> bool {
        const char* ptr = static_cast<const char*>(data);
        while (size > 0) {
            auto written = http->Write(ptr, size);
            if (!written || *written <= 0 || static_cast<size_t>(*written) > size) {
                return false;
            }
            ptr += *written;
            size -= static_cast<size_t>(*written);
        }
        return true;
    };

    bool uploaded = write_all(head.data(), head.size()) && write_all(photo_, photo_len_) &&
                    write_all(tail.data(), tail.size());
    if (!uploaded) {
        ESP_LOGE(TAG, "Failed to upload photo body");
        http->Close();
        return std::unexpected("Failed to upload photo");
    }

    auto status_code = http->GetStatusCode();
    if (!status_code) {
        ESP_LOGE(TAG, "Failed to read HTTP status: %s", status_code.error().ToString().c_str());
        http->Close();
        return std::unexpected("Failed to upload photo");
    }
    if (*status_code != 200) {
        ESP_LOGE(TAG, "Failed to upload photo, status code: %d", *status_code);
        http->Close();
        return std::unexpected("Failed to upload photo");
    }

    std::string result = http->ReadAll();
    http->Close();
    ESP_LOGI(TAG, "Explain %dx%d jpeg=%u bytes, question=%s\n%s", width_, height_,
             (unsigned)photo_len_, question.c_str(), result.c_str());
    return result;
}
