#pragma once

#include <esp_http_server.h>

#include <atomic>
#include <functional>
#include <string>

class MotorController;
class AudioCodec;
class PetMode;

// Phone control page served by Chitu on the home network.
//   http://<robot-ip>/          redirects to the secure page (phones only allow the
//                               microphone on https pages)
//   https://<robot-ip>/         control page
//   https://<robot-ip>/cmd      drive / stop / roam / volume / audio / camera commands
//   https://<robot-ip>/status   JSON status for the page
//   https://<robot-ip>/msg      POST a text message (with emoji) to show on Chitu's screen
//   wss://<robot-ip>/ws         live camera + two-way audio (one phone at a time)
class WebConsole {
public:
    using MessageHandler = std::function<void(const std::string& text, bool say_it)>;

    void Start(MotorController* motors, AudioCodec* codec, PetMode* pet);
    void SetMessageHandler(MessageHandler handler) { on_message_ = std::move(handler); }
    bool running() const { return secure_server_ != nullptr; }
    std::string Url() const;
    // True while someone used the console recently (pet mode stays calm then)
    bool RecentlyUsed() const;

private:
    httpd_handle_t secure_server_ = nullptr;
    httpd_handle_t redirect_server_ = nullptr;
    MotorController* motors_ = nullptr;
    AudioCodec* codec_ = nullptr;
    PetMode* pet_ = nullptr;
    MessageHandler on_message_;

    std::atomic<int> client_fd_{-1};
    std::atomic<int64_t> last_rx_us_{0};
    std::atomic<int64_t> last_use_us_{0};
    std::atomic<bool> video_on_{true};
    uint8_t* tx_buf_ = nullptr;
    size_t tx_cap_ = 0;
    uint8_t* rx_buf_ = nullptr;

    void MediaLoop();
    void DropClient(const char* why);
    bool EnsureTx(size_t len);
    bool SendTx(int fd, size_t len);
    bool SendBinary(int fd, uint8_t type, const void* data, size_t len);

    static esp_err_t HandleIndex(httpd_req_t* req);
    static esp_err_t HandleCmd(httpd_req_t* req);
    static esp_err_t HandleStatus(httpd_req_t* req);
    static esp_err_t HandleMsg(httpd_req_t* req);
    static esp_err_t HandleWs(httpd_req_t* req);
    static esp_err_t HandleRedirect(httpd_req_t* req);
};
