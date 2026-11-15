#include "motor_controller.h"

#include "config.h"
#include "mcp_server.h"

#include <driver/ledc.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>

#include <algorithm>
#include <string>

#define TAG "ChituMotor"

namespace {
constexpr ledc_mode_t kSpeedMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t kTimer = LEDC_TIMER_3;  // timer 1: LED, 2: camera XCLK
constexpr ledc_channel_t kChannel = LEDC_CHANNEL_5;
constexpr ledc_timer_bit_t kResolution = LEDC_TIMER_10_BIT;
constexpr uint32_t kMaxDuty = (1u << 10) - 1;
constexpr int kRampSteps = 8;
constexpr int kRampStepMs = 15;
constexpr int kPollMs = 40;  // obstacle check interval while moving

#if MOTOR_LEFT_IS_A
constexpr gpio_num_t kLeftIn1 = MOTOR_A_IN1_GPIO;
constexpr gpio_num_t kLeftIn2 = MOTOR_A_IN2_GPIO;
constexpr gpio_num_t kRightIn1 = MOTOR_B_IN1_GPIO;
constexpr gpio_num_t kRightIn2 = MOTOR_B_IN2_GPIO;
#else
constexpr gpio_num_t kLeftIn1 = MOTOR_B_IN1_GPIO;
constexpr gpio_num_t kLeftIn2 = MOTOR_B_IN2_GPIO;
constexpr gpio_num_t kRightIn1 = MOTOR_A_IN1_GPIO;
constexpr gpio_num_t kRightIn2 = MOTOR_A_IN2_GPIO;
#endif

// MCP tool error reply (xiaozhi builds without C++ exceptions)
std::string ErrorJson(const std::string& message) {
    return "{\"ok\":false,\"error\":\"" + message + "\"}";
}

inline uint32_t RandRange(uint32_t lo, uint32_t hi) { return lo + esp_random() % (hi - lo + 1); }
}  // namespace

MotorController::MotorController() {}

const char* MotorController::DirName(Dir d) {
    switch (d) {
        case Dir::kForward:
            return "forward";
        case Dir::kBackward:
            return "backward";
        default:
            return "stopped";
    }
}

void MotorController::Initialize() {
    // PWM low first so the driver stays off while we configure direction pins
    ledc_timer_config_t timer_config = {};
    timer_config.speed_mode = kSpeedMode;
    timer_config.duty_resolution = kResolution;
    timer_config.timer_num = kTimer;
    timer_config.freq_hz = MOTOR_PWM_FREQ_HZ;
    timer_config.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

    ledc_channel_config_t channel_config = {};
    channel_config.gpio_num = MOTOR_PWM_GPIO;
    channel_config.speed_mode = kSpeedMode;
    channel_config.channel = kChannel;
    channel_config.timer_sel = kTimer;
    channel_config.duty = 0;
    channel_config.hpoint = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&channel_config));

    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << MOTOR_A_IN1_GPIO) | (1ULL << MOTOR_A_IN2_GPIO) |
                      (1ULL << MOTOR_B_IN1_GPIO) | (1ULL << MOTOR_B_IN2_GPIO);
    io.mode = GPIO_MODE_OUTPUT;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(MOTOR_A_IN1_GPIO, 0);
    gpio_set_level(MOTOR_A_IN2_GPIO, 0);
    gpio_set_level(MOTOR_B_IN1_GPIO, 0);
    gpio_set_level(MOTOR_B_IN2_GPIO, 0);

    queue_ = xQueueCreate(4, sizeof(Command));
    if (queue_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create command queue");
        return;
    }
    if (xTaskCreate(TaskEntry, "chitu_motor", 3584, this, 5, &task_) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create motor task");
        task_ = nullptr;
        return;
    }
    ESP_LOGI(TAG, "Motor controller ready (PWM GPIO%d @ %d Hz)", MOTOR_PWM_GPIO, MOTOR_PWM_FREQ_HZ);
}

bool MotorController::ObstacleAhead() const {
    if (!distance_) {
        return false;
    }
    int mm = distance_();
    return mm > 0 && mm < OBSTACLE_STOP_MM;
}

bool MotorController::Drive(Dir left, Dir right, int speed, int duration_ms) {
    if (queue_ == nullptr) {
        return false;
    }
    Command cmd = {
        .left = left,
        .right = right,
        .speed = std::clamp(speed, 0, 100),
        .duration_ms = std::clamp(duration_ms, 0, MOTOR_MAX_DURATION_MS),
    };
    // Newest command wins: drop anything not yet started
    xQueueReset(queue_);
    return xQueueSend(queue_, &cmd, pdMS_TO_TICKS(50)) == pdTRUE;
}

bool MotorController::DriveNamed(const std::string& direction, int speed, int duration_ms) {
    if (direction == "forward") {
        return Drive(Dir::kForward, Dir::kForward, speed, duration_ms);
    } else if (direction == "backward") {
        return Drive(Dir::kBackward, Dir::kBackward, speed, duration_ms);
    } else if (direction == "left") {
        return Drive(Dir::kBackward, Dir::kForward, speed, duration_ms);
    } else if (direction == "right") {
        return Drive(Dir::kForward, Dir::kBackward, speed, duration_ms);
    }
    return false;
}

void MotorController::Stop() {
    StopRoam();
    StopDance();
    Drive(Dir::kStop, Dir::kStop, 0, 0);
}

void MotorController::TaskEntry(void* arg) {
    static_cast<MotorController*>(arg)->TaskLoop();
}

void MotorController::TaskLoop() {
    Command cmd;
    while (true) {
        if (xQueueReceive(queue_, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        // Keep executing as long as new commands arrive before the current one expires
        bool have_cmd = true;
        while (have_cmd) {
            bool is_stop = (cmd.left == Dir::kStop && cmd.right == Dir::kStop) || cmd.speed == 0 ||
                           cmd.duration_ms == 0;
            bool forward_move = cmd.left == Dir::kForward && cmd.right == Dir::kForward;
            if (is_stop || (forward_move && ObstacleAhead())) {
                if (!is_stop) {
                    obstacle_stop_ = true;
                    ESP_LOGW(TAG, "Obstacle at %d mm - not moving forward", distance_mm());
                }
                ApplyStop();
                break;
            }
            obstacle_stop_ = false;
            Apply(cmd);

            // Wait for the duration in small slices so obstacles stop us quickly
            int64_t end_us = esp_timer_get_time() + (int64_t)cmd.duration_ms * 1000;
            have_cmd = false;
            while (true) {
                int64_t left_ms = (end_us - esp_timer_get_time()) / 1000;
                if (left_ms <= 0) {
                    break;
                }
                if (xQueueReceive(queue_, &cmd, pdMS_TO_TICKS(std::min<int64_t>(left_ms, kPollMs))) ==
                    pdTRUE) {
                    have_cmd = true;  // a newer command replaces this one
                    break;
                }
                if (forward_move && ObstacleAhead()) {
                    obstacle_stop_ = true;
                    ESP_LOGW(TAG, "Obstacle at %d mm - stopping", distance_mm());
                    break;
                }
            }
            if (!have_cmd) {
                ApplyStop();  // time is up or obstacle
            }
        }
    }
}

uint32_t MotorController::SpeedToDuty(int speed) const {
    if (speed <= 0) {
        return 0;
    }
    int pct = MOTOR_MIN_DUTY_PERCENT + (100 - MOTOR_MIN_DUTY_PERCENT) * speed / 100;
    return kMaxDuty * std::clamp(pct, 0, 100) / 100;
}

void MotorController::SetDuty(uint32_t duty) {
    ledc_set_duty(kSpeedMode, kChannel, duty);
    ledc_update_duty(kSpeedMode, kChannel);
    current_duty_ = duty;
}

void MotorController::RampTo(uint32_t target) {
    if (target == current_duty_) {
        return;  // repeated commands (phone pad, roaming) cost no time
    }
    int32_t start = static_cast<int32_t>(current_duty_);
    int32_t delta = static_cast<int32_t>(target) - start;
    for (int i = 1; i <= kRampSteps; i++) {
        SetDuty(static_cast<uint32_t>(start + delta * i / kRampSteps));
        vTaskDelay(pdMS_TO_TICKS(kRampStepMs));
    }
}

void MotorController::SetWheel(bool left, Dir dir) {
    gpio_num_t in1 = left ? kLeftIn1 : kRightIn1;
    gpio_num_t in2 = left ? kLeftIn2 : kRightIn2;
    bool invert = left ? MOTOR_LEFT_INVERT : MOTOR_RIGHT_INVERT;
    if (invert && dir != Dir::kStop) {
        dir = (dir == Dir::kForward) ? Dir::kBackward : Dir::kForward;
    }
    // TB6612: IN1=H IN2=L -> CW, IN1=L IN2=H -> CCW, L/L -> stop (coast)
    gpio_set_level(in1, dir == Dir::kForward ? 1 : 0);
    gpio_set_level(in2, dir == Dir::kBackward ? 1 : 0);
}

void MotorController::Apply(const Command& cmd) {
    bool direction_changed = static_cast<int>(cmd.left) != left_dir_.load() ||
                             static_cast<int>(cmd.right) != right_dir_.load();
    if (direction_changed && current_duty_ > 0) {
        RampTo(0);  // never reverse a spinning motor at full power
    }
    if (direction_changed) {
        SetWheel(true, cmd.left);
        SetWheel(false, cmd.right);
        left_dir_ = static_cast<int>(cmd.left);
        right_dir_ = static_cast<int>(cmd.right);
        if (move_cb_) {
            move_cb_(cmd.left, cmd.right);
        }
        ESP_LOGI(TAG, "Move: left=%s right=%s speed=%d%% duration=%dms", DirName(cmd.left),
                 DirName(cmd.right), cmd.speed, cmd.duration_ms);
    }
    RampTo(SpeedToDuty(cmd.speed));
    speed_ = cmd.speed;
    moving_ = true;
}

void MotorController::ApplyStop() {
    if (current_duty_ > 0) {
        RampTo(0);
    }
    SetDuty(0);
    SetWheel(true, Dir::kStop);
    SetWheel(false, Dir::kStop);
    bool was_moving = moving_.exchange(false);
    speed_ = 0;
    left_dir_ = 0;
    right_dir_ = 0;
    if (was_moving) {
        ESP_LOGI(TAG, "Stopped");
        if (move_cb_) {
            move_cb_(Dir::kStop, Dir::kStop);
        }
    }
}

// ---------------------------------------------------------------------------
// Roaming
// ---------------------------------------------------------------------------
bool MotorController::StartRoam(int minutes) {
    if (!has_distance_sensor()) {
        return false;
    }
    minutes = std::clamp(minutes, 1, ROAM_MAX_MINUTES);
    roam_until_us_ = esp_timer_get_time() + (int64_t)minutes * 60 * 1000 * 1000;
    if (roaming_.exchange(true)) {
        return true;  // already roaming: just extend the time
    }
    if (roam_task_ != nullptr) {
        return true;  // previous roam task is still finishing; it will see roaming_ again
    }
    if (xTaskCreate(RoamEntry, "chitu_roam", 3072, this, 4, &roam_task_) != pdPASS) {
        roaming_ = false;
        return false;
    }
    ESP_LOGI(TAG, "Roaming for %d min", minutes);
    return true;
}

void MotorController::StopRoam() {
    roaming_ = false;  // the roam task notices and exits
}

void MotorController::RoamEntry(void* arg) {
    auto* self = static_cast<MotorController*>(arg);
    self->RoamLoop();
    self->roam_task_ = nullptr;
    vTaskDelete(nullptr);
}

void MotorController::RoamLoop() {
    // Wait in small steps so "stop" ends roaming quickly
    auto pause = [this](int ms) {
        for (int t = 0; t < ms && roaming_; t += 50) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        return roaming_.load();
    };

    while (roaming_ && esp_timer_get_time() < roam_until_us_) {
        int mm = distance_mm();
        if (mm < 0) {
            ESP_LOGW(TAG, "Distance sensor lost - roaming stopped");
            break;
        }
        if (mm < ROAM_AVOID_MM) {
            // Obstacle: stop, back off, turn a random amount, carry on
            Drive(Dir::kStop, Dir::kStop, 0, 0);
            if (!pause(250)) break;
            Drive(Dir::kBackward, Dir::kBackward, ROAM_SPEED, 450);
            if (!pause(500)) break;
            bool turn_left = esp_random() & 1;
            int turn_ms = RandRange(ROAM_TURN_MS_MIN, ROAM_TURN_MS_MAX);
            if (turn_left) {
                Drive(Dir::kBackward, Dir::kForward, ROAM_SPEED + 10, turn_ms);
            } else {
                Drive(Dir::kForward, Dir::kBackward, ROAM_SPEED + 10, turn_ms);
            }
            if (!pause(turn_ms + 100)) break;
        } else {
            // Path clear: keep rolling (re-sent so it never runs unattended for long)
            Drive(Dir::kForward, Dir::kForward, ROAM_SPEED, 400);
            if (!pause(150)) break;
        }
    }
    bool was_roaming = roaming_.exchange(false);
    if (was_roaming) {
        Drive(Dir::kStop, Dir::kStop, 0, 0);  // time is up (a manual stop already stopped us)
    }
    ESP_LOGI(TAG, "Roaming ended");
}

bool MotorController::StartDance(int seconds) {
    seconds = std::clamp(seconds, 3, 120);
    StopRoam();
    dance_until_us_ = esp_timer_get_time() + (int64_t)seconds * 1000 * 1000;
    if (dancing_.exchange(true) || dance_task_ != nullptr) {
        return true;  // already dancing: just extend the time
    }
    if (xTaskCreate(DanceEntry, "chitu_dance", 3072, this, 4, &dance_task_) != pdPASS) {
        dancing_ = false;
        return false;
    }
    ESP_LOGI(TAG, "Dancing for %d s", seconds);
    return true;
}

void MotorController::StopDance() {
    dancing_ = false;
}

void MotorController::DanceEntry(void* arg) {
    auto* self = static_cast<MotorController*>(arg);
    self->DanceLoop();
    self->dance_task_ = nullptr;
    vTaskDelete(nullptr);
}

void MotorController::DanceLoop() {
    auto pause = [this](int ms) {
        for (int t = 0; t < ms && dancing_; t += 50) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        return dancing_.load();
    };
    // A short routine of wiggles, shuffles and spins, repeated until time is up.
    // Moves are small and balanced (left = right, forward = back) so Chitu stays in place.
    struct Step { Dir l; Dir r; int speed; int ms; };
    static const Step kRoutine[] = {
        {Dir::kBackward, Dir::kForward, 70, 250}, {Dir::kForward, Dir::kBackward, 70, 250},
        {Dir::kBackward, Dir::kForward, 70, 250}, {Dir::kForward, Dir::kBackward, 70, 250},
        {Dir::kForward, Dir::kForward, 60, 300},  {Dir::kBackward, Dir::kBackward, 60, 300},
        {Dir::kForward, Dir::kStop, 75, 400},     {Dir::kStop, Dir::kForward, 75, 400},
        {Dir::kBackward, Dir::kStop, 75, 400},    {Dir::kStop, Dir::kBackward, 75, 400},
        {Dir::kForward, Dir::kBackward, 85, 900}, {Dir::kStop, Dir::kStop, 0, 250},
        {Dir::kBackward, Dir::kForward, 85, 900}, {Dir::kStop, Dir::kStop, 0, 250},
    };
    while (dancing_ && esp_timer_get_time() < dance_until_us_) {
        for (const auto& st : kRoutine) {
            if (!dancing_ || esp_timer_get_time() >= dance_until_us_) break;
            Drive(st.l, st.r, st.speed, st.ms);
            if (!pause(st.ms + 30)) break;
        }
    }
    if (dancing_.exchange(false)) {
        Drive(Dir::kStop, Dir::kStop, 0, 0);
    }
    ESP_LOGI(TAG, "Dance ended");
}

std::string MotorController::StatusJson() const {
    return std::string("{\"moving\":") + (moving() ? "true" : "false") +
           ",\"roaming\":" + (roaming() ? "true" : "false") +
           ",\"dancing\":" + (dancing() ? "true" : "false") + ",\"left\":\"" +
           DirName(left_dir()) + "\",\"right\":\"" + DirName(right_dir()) +
           "\",\"speed\":" + std::to_string(speed()) +
           ",\"distance_mm\":" + std::to_string(distance_mm()) +
           ",\"obstacle_stop\":" + (obstacle_stop_ ? "true" : "false") + "}";
}

// ---------------------------------------------------------------------------
// MCP tools
// ---------------------------------------------------------------------------
void MotorController::RegisterMcpTools() {
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool(
        "self.chitu.drive",
        "Move Chitu's tank-tread base (Chitu is this robot). "
        "direction: forward, backward, left or right (left/right spin the whole robot in place). "
        "speed: 1-100 percent (default 70). duration_ms: how long to move, 100-10000 ms. "
        "About 1000 ms is a short step, 3000-6000 ms crosses part of a room, a 90 degree turn "
        "is roughly 400-600 ms. The robot stops by itself when the time is up, and forward "
        "moves stop automatically in front of obstacles. To wander around on its own, use "
        "self.chitu.roam instead. Use self.chitu.run_motor for only one wheel/motor.",
        PropertyList({
            Property("direction", kPropertyTypeString, std::string("forward")),
            Property("speed", kPropertyTypeInteger, 70, 1, 100),
            Property("duration_ms", kPropertyTypeInteger, 1000, 100, MOTOR_MAX_DURATION_MS),
        }),
        [this](const PropertyList& properties) -> ReturnValue {
            auto direction = properties["direction"].value<std::string>();
            int speed = properties["speed"].value<int>();
            int duration = properties["duration_ms"].value<int>();
            StopRoam();
            if (direction == "forward" && ObstacleAhead()) {
                return ErrorJson("Obstacle ahead at " + std::to_string(distance_mm() / 10) +
                                 " cm, cannot move forward. Turn or back up first.");
            }
            if (!DriveNamed(direction, speed, duration)) {
                return ErrorJson("Invalid direction. Use forward, backward, left or right.");
            }
            return std::string("{\"ok\":true,\"direction\":\"") + direction +
                   "\",\"speed\":" + std::to_string(speed) +
                   ",\"duration_ms\":" + std::to_string(duration) + "}";
        });

    mcp.AddTool(
        "self.chitu.run_motor",
        "Run exactly one of Chitu's wheels while the other stays stopped. "
        "Use this when the user asks for only the left or only the right motor/wheel. "
        "motor: left or right. direction: forward or backward. speed: 1-100. duration_ms: 100-10000.",
        PropertyList({
            Property("motor", kPropertyTypeString, std::string("left")),
            Property("direction", kPropertyTypeString, std::string("forward")),
            Property("speed", kPropertyTypeInteger, 70, 1, 100),
            Property("duration_ms", kPropertyTypeInteger, 1000, 100, MOTOR_MAX_DURATION_MS),
        }),
        [this](const PropertyList& properties) -> ReturnValue {
            auto motor = properties["motor"].value<std::string>();
            auto direction = properties["direction"].value<std::string>();
            int speed = properties["speed"].value<int>();
            int duration = properties["duration_ms"].value<int>();
            if (motor != "left" && motor != "right") {
                return ErrorJson("Invalid motor. Use left or right.");
            }
            Dir d;
            if (direction == "forward") {
                d = Dir::kForward;
            } else if (direction == "backward") {
                d = Dir::kBackward;
            } else {
                return ErrorJson("Invalid direction. Use forward or backward.");
            }
            StopRoam();
            bool ok = (motor == "left") ? Drive(d, Dir::kStop, speed, duration)
                                        : Drive(Dir::kStop, d, speed, duration);
            if (!ok) {
                return ErrorJson("Motor command queue is unavailable.");
            }
            return std::string("{\"ok\":true,\"motor\":\"") + motor + "\",\"direction\":\"" +
                   direction + "\",\"other_motor\":\"stopped\",\"duration_ms\":" +
                   std::to_string(duration) + "}";
        });

    mcp.AddTool(
        "self.chitu.roam",
        "Start or stop Chitu exploring on its own: it drives around, avoids obstacles with its "
        "distance sensor and turns away from walls. Use this when the user asks Chitu to move "
        "around, explore, wander or patrol. action: start or stop. minutes: how long to roam "
        "(1-30, default 3). Say stop or call self.chitu.stop to end it early.",
        PropertyList({
            Property("action", kPropertyTypeString, std::string("start")),
            Property("minutes", kPropertyTypeInteger, 3, 1, ROAM_MAX_MINUTES),
        }),
        [this](const PropertyList& properties) -> ReturnValue {
            auto action = properties["action"].value<std::string>();
            int minutes = properties["minutes"].value<int>();
            if (action == "stop") {
                Stop();
                return std::string("{\"ok\":true,\"roaming\":false}");
            }
            if (action != "start") {
                return ErrorJson("Invalid action. Use start or stop.");
            }
            if (!StartRoam(minutes)) {
                return ErrorJson(
                    "Cannot roam: the distance sensor is not connected, so it is not safe to "
                    "drive on my own. Use self.chitu.drive for short moves instead.");
            }
            return std::string("{\"ok\":true,\"roaming\":true,\"minutes\":") +
                   std::to_string(minutes) + "}";
        });

    mcp.AddTool(
        "self.chitu.dance",
        "Make Chitu dance: wiggles, shuffles and spins in place. It runs in the background and "
        "returns immediately, so call this FIRST and then start playing the music or singing - "
        "Chitu keeps dancing while the music plays. seconds: how long to dance (3-120, default "
        "30; use about the length of the song). Call self.chitu.stop to end early.",
        PropertyList({Property("seconds", kPropertyTypeInteger, 30, 3, 120)}),
        [this](const PropertyList& properties) -> ReturnValue {
            int seconds = properties["seconds"].value<int>();
            if (!StartDance(seconds)) {
                return ErrorJson("Could not start dancing.");
            }
            return std::string("{\"ok\":true,\"dancing\":true,\"seconds\":") +
                   std::to_string(seconds) + ",\"note\":\"Dancing in the background - start the music now.\"}";
        });

    mcp.AddTool("self.chitu.stop", "Stop Chitu's motors immediately (also ends roaming and dancing).",
                PropertyList(), [this](const PropertyList&) -> ReturnValue {
                    Stop();
                    return true;
                });

    mcp.AddTool("self.chitu.get_status",
                "Get Chitu's motor status: moving, roaming, wheel directions, speed, and the "
                "distance to the nearest obstacle ahead in mm (-1 = no sensor).",
                PropertyList(),
                [this](const PropertyList&) -> ReturnValue { return StatusJson(); });
}
