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
// With a distance sensor attached it stops before obstacles and can roam on its own.
class MotorController {
public:
    enum class Dir : int8_t { kBackward = -1, kStop = 0, kForward = 1 };

    // Called on every new movement so the face can react (e.g. look left on a left turn).
    using MoveCallback = std::function<void(Dir left, Dir right)>;
    // Returns the distance ahead in mm, or -1 if unknown.
    using DistanceSource = std::function<int()>;

    MotorController();
    ~MotorController() = default;

    void Initialize();
    void SetMoveCallback(MoveCallback cb) { move_cb_ = std::move(cb); }
    void SetDistanceSource(DistanceSource src) { distance_ = std::move(src); }

    // speed 0-100 (%), duration in ms (clamped to MOTOR_MAX_DURATION_MS)
    bool Drive(Dir left, Dir right, int speed, int duration_ms);
    // Named directions: forward, backward, left, right. Returns false if unknown.
    bool DriveNamed(const std::string& direction, int speed, int duration_ms);
    void Stop();  // also ends roaming

    // Autonomous roaming with obstacle avoidance (needs the distance sensor)
    bool StartRoam(int minutes);
    void StopRoam();
    bool roaming() const { return roaming_.load(); }

    // Dance moves in the background (non-blocking) so music can play at the same time
    bool StartDance(int seconds);
    void StopDance();
    bool dancing() const { return dancing_.load(); }

    bool moving() const { return moving_.load(); }
    int speed() const { return speed_.load(); }
    Dir left_dir() const { return static_cast<Dir>(left_dir_.load()); }
    Dir right_dir() const { return static_cast<Dir>(right_dir_.load()); }
    int distance_mm() const { return distance_ ? distance_() : -1; }
    bool has_distance_sensor() const { return distance_ && distance_() >= 0; }

    static const char* DirName(Dir d);
    std::string StatusJson() const;

    // Registers the self.chitu.* MCP tools
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
    DistanceSource distance_;

    std::atomic<bool> moving_{false};
    std::atomic<int> speed_{0};
    std::atomic<int> left_dir_{0};
    std::atomic<int> right_dir_{0};
    std::atomic<bool> obstacle_stop_{false};
    uint32_t current_duty_ = 0;

    std::atomic<bool> roaming_{false};
    std::atomic<int64_t> roam_until_us_{0};
    TaskHandle_t roam_task_ = nullptr;

    static void TaskEntry(void* arg);
    void TaskLoop();
    void Apply(const Command& cmd);
    void ApplyStop();
    void SetWheel(bool left, Dir dir);
    void SetDuty(uint32_t duty);
    void RampTo(uint32_t target_duty);
    uint32_t SpeedToDuty(int speed) const;
    bool ObstacleAhead() const;

    static void RoamEntry(void* arg);
    void RoamLoop();

    std::atomic<bool> dancing_{false};
    std::atomic<int64_t> dance_until_us_{0};
    TaskHandle_t dance_task_ = nullptr;
    static void DanceEntry(void* arg);
    void DanceLoop();
};
