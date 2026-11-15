#pragma once

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <atomic>
#include <functional>
#include <string>

// Drives a TB6612FNG with one shared PWM line (PWMA+PWMB tied together)
// and four direction pins. Commands are queued and executed by a task, and
// every command stops automatically after its duration (safety).
class MotorController {
public:
    enum class Dir : int8_t { kBackward = -1, kStop = 0, kForward = 1 };

    // Called on every new movement so the face can react (e.g. look left on a left turn).
    // left/right are the wheel directions; both kStop means stopped.
    using MoveCallback = std::function<void(Dir left, Dir right)>;

    MotorController();
    ~MotorController() = default;

    void Initialize();
    void SetMoveCallback(MoveCallback cb) { move_cb_ = std::move(cb); }

    // speed 0-100 (%), duration in ms (clamped to MOTOR_MAX_DURATION_MS)
    bool Drive(Dir left, Dir right, int speed, int duration_ms);
    void Stop();

    bool moving() const { return moving_.load(); }
    int speed() const { return speed_.load(); }
    Dir left_dir() const { return static_cast<Dir>(left_dir_.load()); }
    Dir right_dir() const { return static_cast<Dir>(right_dir_.load()); }

    static const char* DirName(Dir d);

    // Registers the self.wall_e.* MCP tools
    void RegisterMcpTools();

private:
    struct Command {
        Dir left;
        Dir right;
        int speed;
        int duration_ms;
    };

    QueueHandle_t queue_ = nullptr;
    TaskHandle_t task_ = nullptr;
    MoveCallback move_cb_;

    std::atomic<bool> moving_{false};
    std::atomic<int> speed_{0};
    std::atomic<int> left_dir_{0};
    std::atomic<int> right_dir_{0};
    uint32_t current_duty_ = 0;

    static void TaskEntry(void* arg);
    void TaskLoop();
    void Apply(const Command& cmd);
    void ApplyStop();
    void SetWheel(bool left, Dir dir);
    void SetDuty(uint32_t duty);
    void RampTo(uint32_t target_duty);
    uint32_t SpeedToDuty(int speed) const;
};
