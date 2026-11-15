#pragma once

#include <esp_http_server.h>

#include <atomic>
#include <string>

class MotorController;
class AudioCodec;

// Phone control page served by the robot on the home network:
//   http://<robot-ip>/        control page (drive pad, speed, manual/auto, status)
//   http://<robot-ip>/cmd     drive / stop / roam / volume commands
//   http://<robot-ip>/status  JSON status for the page
//   http://<robot-ip>:81/stream  live MJPEG camera stream (separate server so the
//                                endless stream never blocks the controls)
class WebConsole {
public:
    void Start(MotorController* motors, AudioCodec* codec);
    bool running() const { return control_server_ != nullptr; }
    std::string Url() const;

private:
    httpd_handle_t control_server_ = nullptr;
    httpd_handle_t stream_server_ = nullptr;
    MotorController* motors_ = nullptr;
    AudioCodec* codec_ = nullptr;
    std::atomic<int> stream_generation_{0};

    static esp_err_t HandleIndex(httpd_req_t* req);
    static esp_err_t HandleCmd(httpd_req_t* req);
    static esp_err_t HandleStatus(httpd_req_t* req);
    static esp_err_t HandleStream(httpd_req_t* req);
};
