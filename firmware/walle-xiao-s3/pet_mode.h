#pragma once

#include <atomic>
#include <cstdint>
#include <functional>

class MotorController;
class Display;

// Makes Chitu act like a little pet while nobody is talking to it:
// looks around, pulls faces, chirps, wiggles, dozes off after a while
// and now and then says something on its own ("chatty").
class PetMode {
public:
    using LookFn = std::function<void(float x, float y)>;
    using BusyFn = std::function<bool()>;

    void Start(MotorController* motors, Display* display, LookFn look, BusyFn busy);

    bool enabled() const { return enabled_.load(); }
    bool chatty() const { return chatty_.load(); }
    void SetEnabled(bool on);
    void SetChatty(bool on);

    // Someone interacted (voice, console, message): wake up and pay attention
    void Poke();

    void RegisterMcpTools();

private:
    MotorController* motors_ = nullptr;
    Display* display_ = nullptr;
    LookFn look_;
    BusyFn busy_;
    std::atomic<bool> enabled_{true};
    std::atomic<bool> chatty_{true};
    std::atomic<int64_t> last_active_us_{0};
    std::atomic<bool> sleeping_{false};
    int64_t next_action_us_ = 0;
    int64_t next_chat_us_ = 0;

    void Loop();
    void DoAction();
    void Mood(const char* emotion, int ms);
    bool QuietHours() const;
    void Sleep(bool on);
};
