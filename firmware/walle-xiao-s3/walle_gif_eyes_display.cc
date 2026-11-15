#include "config.h"
#if WALLE_EYES_STYLE == 1

#include "walle_gif_eyes_display.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_random.h>

#include <cstring>
#include <string>

#define TAG "WallEGifEyes"

namespace {
// The mono OLED port lights "dark" pixels: black = lit, white = off.
constexpr uint16_t kLit = 0x0000;
constexpr uint16_t kOff = 0xFFFF;
constexpr uint32_t kTickMs = 20;

inline uint32_t RandRange(uint32_t lo, uint32_t hi) { return lo + esp_random() % (hi - lo + 1); }
}  // namespace

WallEGifEyesDisplay::WallEGifEyesDisplay(esp_lcd_panel_io_handle_t panel_io,
                                   esp_lcd_panel_handle_t panel, int width, int height,
                                   bool mirror_x, bool mirror_y)
    : OledDisplay(panel_io, panel, width, height, mirror_x, mirror_y) {}

WallEGifEyesDisplay::~WallEGifEyesDisplay() {
    DisplayLockGuard lock(this);
    if (timer_ != nullptr) {
        lv_timer_delete(timer_);
    }
    if (layer_ != nullptr) {
        lv_obj_delete(layer_);
    }
    if (canvas_buf_ != nullptr) {
        heap_caps_free(canvas_buf_);
    }
}

void WallEGifEyesDisplay::SetupUI() {
    OledDisplay::SetupUI();  // keeps the standard UI underneath for system messages
    if (layer_ != nullptr) {
        return;
    }

    DisplayLockGuard lock(this);
    canvas_buf_ = static_cast<uint16_t*>(
        heap_caps_malloc(WALLE_EYE_W * WALLE_EYE_H * sizeof(uint16_t), MALLOC_CAP_DEFAULT));
    if (canvas_buf_ == nullptr) {
        ESP_LOGE(TAG, "No memory for the eye canvas");
        return;
    }

    // Full-screen black (off) layer on top of everything, like the original robot
    layer_ = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(layer_);
    lv_obj_set_size(layer_, width_, height_);
    lv_obj_set_pos(layer_, 0, 0);
    lv_obj_set_style_bg_color(layer_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(layer_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(layer_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(layer_);

    canvas_ = lv_canvas_create(layer_);
    lv_canvas_set_buffer(canvas_, canvas_buf_, WALLE_EYE_W, WALLE_EYE_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(canvas_, (width_ - WALLE_EYE_W) / 2, (height_ - WALLE_EYE_H) / 2);

    const walle_eye_anim_t* normal = FindAnim("normal");
    if (normal != nullptr) {
        DrawFrame(normal->frames[0]);
    }
    uint32_t now = lv_tick_get();
    next_idle_ms_ = now + 1500;
    timer_ = lv_timer_create(TimerCb, kTickMs, this);
    ESP_LOGI(TAG, "Wall-E eyes ready (%d animations)", walle_eye_anim_count);
}

// ---------------------------------------------------------------------------
// Animation lookup
// ---------------------------------------------------------------------------
const walle_eye_anim_t* WallEGifEyesDisplay::FindAnim(const char* name) {
    if (name == nullptr) {
        return nullptr;
    }
    for (int i = 0; i < walle_eye_anim_count; i++) {
        if (strcmp(walle_eye_anims[i].name, name) == 0) {
            return &walle_eye_anims[i];
        }
    }
    return nullptr;
}

// xiaozhi emotion names (plus the original firmware's own names) -> eye animation.
// nullptr = no special animation, just keep idling (blinks and glances).
const char* WallEGifEyesDisplay::AnimForEmotion(const char* emotion) {
    std::string e = emotion ? emotion : "";
    if (e == "happy" || e == "laughing" || e == "funny" || e == "loving" || e == "kissy" ||
        e == "delicious" || e == "confident" || e == "cool" || e == "winking" ||
        e == "happy_eyes") {
        return "happy";
    }
    if (e == "sad" || e == "crying" || e == "embarrassed" || e == "sad_eyes") {
        return "sad";
    }
    if (e == "angry" || e == "disgusted" || e == "angry_eyes") {
        return "angry";
    }
    if (e == "suspicious" || e == "confused" || e == "suspicious_eyes") {
        return "suspicious";
    }
    if (e == "surprised" || e == "shocked" || e == "nervous" || e == "silly" || e == "sleepy" ||
        e == "eyes_shake") {
        return "shake";
    }
    if (e == "thinking" || e == "look_up_left" || e == "nhin tren trai") {
        return "look_up_left";
    }
    if (e == "look_up_right" || e == "nhin tren phai") {
        return "look_up_right";
    }
    if (e == "look_down_left" || e == "nhin duoi trai") {
        return "look_down_left";
    }
    if (e == "look_down_right" || e == "nhin duoi phai") {
        return "look_down_right";
    }
    return nullptr;  // neutral, relaxed, standby, unknown
}

// ---------------------------------------------------------------------------
// Playback (all of this runs in the LVGL task or under the display lock)
// ---------------------------------------------------------------------------
void WallEGifEyesDisplay::DrawFrame(uint16_t frame_index) {
    if (canvas_buf_ == nullptr) {
        return;
    }
    const uint8_t* src = walle_eye_frame_data[frame_index];
    uint16_t* dst = canvas_buf_;
    for (int y = 0; y < WALLE_EYE_H; y++) {
        const uint8_t* row = src + y * WALLE_EYE_STRIDE;
        for (int x = 0; x < WALLE_EYE_W; x++) {
            *dst++ = (row[x >> 3] & (0x80 >> (x & 7))) ? kLit : kOff;
        }
    }
    lv_obj_invalidate(canvas_);
}

void WallEGifEyesDisplay::Play(const walle_eye_anim_t* anim, bool is_emotion) {
    if (anim == nullptr || canvas_ == nullptr) {
        return;
    }
    anim_ = anim;
    frame_ = 0;
    emotion_playing_ = is_emotion;
    DrawFrame(anim_->frames[0]);
    next_frame_ms_ = lv_tick_get() + anim_->delays_ms[0];
}

void WallEGifEyesDisplay::ScheduleIdle(uint32_t now) {
    next_idle_ms_ = now + RandRange(2500, 6000);
}

void WallEGifEyesDisplay::TimerCb(lv_timer_t* t) {
    auto* self = static_cast<WallEGifEyesDisplay*>(lv_timer_get_user_data(t));
    if (self->layer_ == nullptr || lv_obj_has_flag(self->layer_, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    uint32_t now = lv_tick_get();

    if (self->anim_ != nullptr) {
        if ((int32_t)(now - self->next_frame_ms_) < 0) {
            return;
        }
        self->frame_++;
        if (self->frame_ >= self->anim_->frame_count) {
            // Finished: animations end on normal open eyes, which stay on screen
            self->anim_ = nullptr;
            self->emotion_playing_ = false;
            self->ScheduleIdle(now);
            return;
        }
        self->DrawFrame(self->anim_->frames[self->frame_]);
        self->next_frame_ms_ = now + self->anim_->delays_ms[self->frame_];
        return;
    }

    // Idle: mostly blink, sometimes look around (same animations as the original)
    if ((int32_t)(now - self->next_idle_ms_) >= 0) {
        static const char* kIdle[] = {"normal",       "normal",        "normal",
                                      "look_up_left", "look_up_right", "look_down_left",
                                      "look_down_right"};
        self->Play(FindAnim(kIdle[esp_random() % (sizeof(kIdle) / sizeof(kIdle[0]))]), false);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
bool WallEGifEyesDisplay::SetEmotion(const char* emotion) {
    DisplayLockGuard lock(this);
    if (layer_ == nullptr) {
        return false;
    }
    ShowEyes(true);
    const walle_eye_anim_t* anim = FindAnim(AnimForEmotion(emotion));
    if (anim == nullptr) {
        return true;  // neutral: keep idling, don't cut a running expression short
    }
    if (emotion_playing_ && anim_ == anim) {
        return true;  // the same expression is already playing
    }
    Play(anim, true);
    return true;
}

void WallEGifEyesDisplay::SetChatMessage(const char* role, const char* content) {
    OledDisplay::SetChatMessage(role, content);
    DisplayLockGuard lock(this);
    bool is_system_text =
        role != nullptr && strcmp(role, "system") == 0 && content != nullptr && content[0] != '\0';
    // Wi-Fi setup / activation instructions must be readable
    ShowEyes(!is_system_text);
}

void WallEGifEyesDisplay::LookAt(float x, float y) {
    DisplayLockGuard lock(this);
    if (layer_ == nullptr || (x == 0.0f && y == 0.0f)) {
        return;
    }
    const char* name;
    if (y > 0.0f) {
        name = (x > 0.0f) ? "look_down_right" : "look_down_left";
    } else {
        name = (x > 0.0f) ? "look_up_right" : "look_up_left";
    }
    Play(FindAnim(name), true);
}

void WallEGifEyesDisplay::ShowEyes(bool show) {
    if (layer_ == nullptr) {
        return;
    }
    if (show) {
        lv_obj_remove_flag(layer_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(layer_, LV_OBJ_FLAG_HIDDEN);
    }
}

#endif  // WALLE_EYES_STYLE == 1
