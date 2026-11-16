#include "pet_mode.h"

#include "application.h"
#include "assets/lang_config.h"
#include "config.h"
#include "display.h"
#include "mcp_server.h"
#include "motor_controller.h"
#include "settings.h"

#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdlib>
#include <ctime>
#include <string>

#define TAG "PetMode"

namespace {
constexpr int64_t kSec = 1000LL * 1000;
int64_t Now() { return esp_timer_get_time(); }
int64_t RandSec(int lo, int hi) { return (lo + (int)(esp_random() % (uint32_t)(hi - lo + 1))) * kSec; }
void Pause(int ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

// What Chitu is asked to do when it speaks up on its own
const char* const kChatPrompts[] = {
    "(Nobody has talked to you for a while. Say one short, cute, playful line out loud to "
    "whoever is nearby, like a happy little pet robot. One sentence, no question.)",
    "(You are bored and idle. Say one short funny thing about being a tiny robot. One sentence.)",
    "(You are idle. Hum a tiny tune with sounds like 'beep boop' and say you are happy. Keep it short.)",
    "(You just woke up from a nap. Stretch with a cute sound and say one short sleepy line.)",
    "(You are idle. Tell everyone nearby one tiny fun fact in one short sentence, excitedly.)",
};
}  // namespace

void PetMode::Start(MotorController* motors, Display* display, LookFn look, BusyFn busy) {
    motors_ = motors;
    display_ = display;
    look_ = std::move(look);
    busy_ = std::move(busy);
    {
        Settings settings("chitu");
        enabled_ = settings.GetInt("pet_on", 1) != 0;
        chatty_ = settings.GetInt("pet_chat", 1) != 0;
    }
    setenv("TZ", PET_TIMEZONE, 1);
    tzset();
    last_active_us_ = Now();
    next_action_us_ = Now() + 30 * kSec;
    next_chat_us_ = Now() + (int64_t)PET_CHAT_MIN_MINUTES * 60 * kSec;
    xTaskCreate([](void* arg) { static_cast<PetMode*>(arg)->Loop(); }, "chitu_pet", 3584, this, 2,
                nullptr);
}

void PetMode::SetEnabled(bool on) {
    enabled_ = on;
    Settings("chitu", true).SetInt("pet_on", on ? 1 : 0);
    Poke();
}

void PetMode::SetChatty(bool on) {
    chatty_ = on;
    Settings("chitu", true).SetInt("pet_chat", on ? 1 : 0);
}

void PetMode::Poke() {
    last_active_us_ = Now();
    if (sleeping_) {
        Sleep(false);
    }
}

bool PetMode::QuietHours() const {
    time_t t = time(nullptr);
    if (t < 1700000000) {
        return false;  // clock not set yet
    }
    struct tm local;
    localtime_r(&t, &local);
    int h = local.tm_hour;
    if (PET_QUIET_FROM_HOUR > PET_QUIET_TO_HOUR) {
        return h >= PET_QUIET_FROM_HOUR || h < PET_QUIET_TO_HOUR;  // e.g. 22..8
    }
    return h >= PET_QUIET_FROM_HOUR && h < PET_QUIET_TO_HOUR;
}

void PetMode::Sleep(bool on) {
    sleeping_ = on;
    if (display_ != nullptr) {
        display_->SetEmotion(on ? "pet_sleep" : "neutral");
    }
    ESP_LOGI(TAG, "%s", on ? "Chitu fell asleep" : "Chitu woke up");
}

void PetMode::Mood(const char* emotion, int ms) {
    if (display_ == nullptr) {
        return;
    }
    display_->SetEmotion(emotion);
    Pause(ms);
    // only go back to calm eyes if nothing else happened meanwhile
    if (Application::GetInstance().GetDeviceState() == kDeviceStateIdle && !sleeping_) {
        display_->SetEmotion("neutral");
    }
}

void PetMode::Loop() {
    DeviceState last_state = kDeviceStateUnknown;
    while (true) {
        Pause(500);
        auto& app = Application::GetInstance();
        DeviceState state = app.GetDeviceState();
        int64_t now = Now();

        bool busy = state != kDeviceStateIdle || (busy_ && busy_()) || motors_->moving() ||
                    motors_->roaming() || motors_->dancing();
        if (busy) {
            last_active_us_ = now;
            if (sleeping_ && state != kDeviceStateIdle) {
                sleeping_ = false;  // the app already changed the face (listening/speaking)
            }
            last_state = state;
            continue;
        }
        if (last_state != kDeviceStateIdle) {
            // just became idle: give people a moment before acting cute
            next_action_us_ = now + RandSec(15, 30);
            last_state = state;
        }
        if (!enabled_) {
            continue;
        }

        int64_t idle = now - last_active_us_.load();

        // Speak up now and then (not at night)
        if (chatty_ && now >= next_chat_us_ && idle > 3 * 60 * kSec && !QuietHours()) {
            next_chat_us_ = now + RandSec(PET_CHAT_MIN_MINUTES * 60, PET_CHAT_MAX_MINUTES * 60);
            const char* prompt =
                kChatPrompts[esp_random() % (sizeof(kChatPrompts) / sizeof(kChatPrompts[0]))];
            ESP_LOGI(TAG, "Chitu speaks up on its own");
            sleeping_ = false;
            app.WakeWordInvoke(prompt);
            continue;
        }

        if (sleeping_) {
            continue;
        }
        if (idle > (int64_t)PET_SLEEP_AFTER_MINUTES * 60 * kSec) {
            Sleep(true);
            continue;
        }
        if (now >= next_action_us_) {
            DoAction();
            next_action_us_ = Now() + RandSec(20, 55);
        }
    }
}

void PetMode::DoAction() {
    auto& app = Application::GetInstance();
    int64_t idle = Now() - last_active_us_.load();
    uint32_t r = esp_random() % 100;

    if (r < 28) {
        // look around the room
        if (look_) {
            static const float kSpots[][2] = {{-1, 0}, {1, 0}, {-0.7f, -0.7f}, {0.7f, -0.6f}, {0, 0.8f}};
            int n = 2 + esp_random() % 2;
            for (int i = 0; i < n; i++) {
                const auto& p = kSpots[esp_random() % 5];
                look_(p[0], p[1]);
                Pause(700 + esp_random() % 600);
            }
            look_(0, 0);
        }
    } else if (r < 60) {
        // pull a face
        static const char* kMoods[] = {"happy", "loving", "silly", "winking", "thinking",
                                       "surprised", "suspicious", "laughing", "embarrassed",
                                       "cool", "confused", "excited"};
        Mood(kMoods[esp_random() % (sizeof(kMoods) / sizeof(kMoods[0]))], 2200 + esp_random() % 1500);
    } else if (r < 76) {
#if PET_MOVES
        // happy wiggle on the spot
        if (!motors_->moving()) {
            if (display_ != nullptr) display_->SetEmotion("excited");
            using D = MotorController::Dir;
            for (int i = 0; i < 2; i++) {
                motors_->Drive(D::kBackward, D::kForward, 55, 160);
                Pause(220);
                motors_->Drive(D::kForward, D::kBackward, 55, 160);
                Pause(220);
            }
            Pause(400);
            if (display_ != nullptr && app.GetDeviceState() == kDeviceStateIdle) {
                display_->SetEmotion("neutral");
            }
        }
#endif
    } else if (r < 90) {
        // little chirp
        if (!QuietHours()) {
            app.PlaySound(Lang::Sounds::OGG_POPUP);
        }
        Mood("surprised", 900);
    } else {
        // yawn / stretch when it has been quiet for a while
        if (idle > 3 * 60 * kSec) {
            Mood("sleepy", 2500);
        } else {
            Mood("happy", 2000);
        }
    }
}

void PetMode::RegisterMcpTools() {
    McpServer::GetInstance().AddTool(
        "self.chitu.pet_mode",
        "Turn Chitu's pet behaviour on or off. When on, Chitu acts alive while idle: looks "
        "around, makes faces, chirps, wiggles and naps. chatty: whether Chitu sometimes speaks "
        "up on its own (never at night). Use when the user asks Chitu to be quiet, stop moving "
        "on its own, or to be more lively.",
        PropertyList({
            Property("enabled", kPropertyTypeBoolean, true),
            Property("chatty", kPropertyTypeBoolean, true),
        }),
        [this](const PropertyList& properties) -> ReturnValue {
            SetEnabled(properties["enabled"].value<bool>());
            SetChatty(properties["chatty"].value<bool>());
            return std::string("{\"ok\":true,\"pet_mode\":") + (enabled() ? "true" : "false") +
                   ",\"chatty\":" + (chatty() ? "true" : "false") + "}";
        });
}
