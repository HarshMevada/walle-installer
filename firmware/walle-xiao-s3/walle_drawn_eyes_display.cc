#include "config.h"
#if WALLE_EYES_STYLE == 0

#include "walle_drawn_eyes_display.h"

#include <esp_log.h>
#include <esp_random.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#define TAG "WallEDrawnEyes"

namespace {
// On the mono OLED port "black" pixels are lit and "white" pixels are off.
inline lv_color_t Lit() { return lv_color_black(); }
inline lv_color_t Off() { return lv_color_white(); }

constexpr int kStatusBarHeight = 16;
constexpr int kEyeW = 36;
constexpr int kEyeH = 34;
constexpr int kEyeRadius = 9;
constexpr int kEyeCenterX[2] = {36, 92};
constexpr float kLookRangeX = 16.0f;
constexpr float kLookRangeY = 7.0f;

inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline uint32_t RandRange(uint32_t lo, uint32_t hi) { return lo + esp_random() % (hi - lo + 1); }

lv_obj_t* MakeBox(lv_obj_t* parent, lv_color_t color) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    return obj;
}
}  // namespace

WallEDrawnEyesDisplay::WallEDrawnEyesDisplay(esp_lcd_panel_io_handle_t panel_io,
                                             esp_lcd_panel_handle_t panel, int width, int height,
                                             bool mirror_x, bool mirror_y)
    : OledDisplay(panel_io, panel, width, height, mirror_x, mirror_y) {}

WallEDrawnEyesDisplay::~WallEDrawnEyesDisplay() {
    DisplayLockGuard lock(this);
    lv_anim_delete(this, nullptr);
    if (idle_timer_ != nullptr) {
        lv_timer_delete(idle_timer_);
        idle_timer_ = nullptr;
    }
    if (layer_ != nullptr) {
        lv_obj_delete(layer_);
        layer_ = nullptr;
    }
}

void WallEDrawnEyesDisplay::SetupUI() {
    OledDisplay::SetupUI();  // status bar, notifications, chat label
    if (layer_ != nullptr) {
        return;
    }

    DisplayLockGuard lock(this);
    lv_obj_t* screen = lv_screen_active();

    layer_ = MakeBox(screen, Off());
    lv_obj_set_size(layer_, width_, height_ - kStatusBarHeight);
    lv_obj_set_pos(layer_, 0, kStatusBarHeight);
    // Above the chat/emoji content, below the status bar
    lv_obj_move_to_index(layer_, 1);

    for (int i = 0; i < 2; i++) {
        eye_[i] = MakeBox(layer_, Lit());
        lv_obj_set_style_radius(eye_[i], kEyeRadius, 0);
        lv_obj_set_style_clip_corner(eye_[i], true, 0);

        top_lid_[i] = MakeBox(eye_[i], Off());
        lv_obj_add_flag(top_lid_[i], LV_OBJ_FLAG_HIDDEN);

        bottom_lid_[i] = MakeBox(eye_[i], Off());
        lv_obj_set_style_radius(bottom_lid_[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_add_flag(bottom_lid_[i], LV_OBJ_FLAG_HIDDEN);
    }

    bool wander = true;
    base_ = ShapeFor("neutral", &wander);
    idle_wander_ = wander;
    current_ = base_;
    from_ = base_;
    to_ = base_;
    Render();

    uint32_t now = lv_tick_get();
    next_blink_ms_ = now + 1500;
    next_glance_ms_ = now + 3000;
    idle_timer_ = lv_timer_create(IdleTimerCb, 100, this);
    ESP_LOGI(TAG, "Wall-E eyes ready");
}

// ---------------------------------------------------------------------------
// Emotion -> eye shape
// ---------------------------------------------------------------------------
WallEDrawnEyesDisplay::Shape WallEDrawnEyesDisplay::ShapeFor(const char* emotion, bool* wander) {
    Shape s;
    *wander = false;
    auto both = [&s](float h, float top, float angle, float bottom) {
        for (auto& e : s.eye) {
            e.h_scale = h;
            e.top = top;
            e.angle = angle;
            e.bottom = bottom;
        }
    };
    std::string e = emotion ? emotion : "neutral";

    if (e == "neutral" || e == "relaxed" || e == "standby" || e == "eyes_nomal") {
        *wander = true;
    } else if (e == "happy" || e == "laughing" || e == "funny" || e == "delicious" ||
               e == "kissy" || e == "happy_eyes") {
        both(0.9f, 0.0f, 0.0f, 0.68f);
        s.dy = -2;
    } else if (e == "loving") {
        both(1.1f, 0.0f, 0.0f, 0.42f);
        s.w_scale = 1.08f;
    } else if (e == "sad" || e == "sad_eyes") {
        both(0.85f, 0.38f, -18.0f, 0.0f);
        s.dy = 4;
    } else if (e == "crying") {
        both(0.8f, 0.45f, -22.0f, 0.0f);
        s.dy = 5;
    } else if (e == "angry" || e == "angry_eyes") {
        both(0.9f, 0.4f, 24.0f, 0.0f);
    } else if (e == "disgusted") {
        both(0.85f, 0.3f, 14.0f, 0.22f);
    } else if (e == "surprised" || e == "shocked") {
        both(1.25f, 0.0f, 0.0f, 0.0f);
        s.w_scale = 1.12f;
        s.dy = -2;
    } else if (e == "thinking") {
        both(0.8f, 0.15f, 0.0f, 0.0f);
        s.dx = 12;
        s.dy = -6;
    } else if (e == "confused") {
        s.eye[0].h_scale = 1.1f;
        s.eye[1].h_scale = 0.65f;
        s.eye[1].top = 0.2f;
        s.eye[1].angle = -12.0f;
    } else if (e == "silly") {
        s.eye[0].h_scale = 1.2f;
        s.eye[1].h_scale = 0.6f;
        s.eye[1].bottom = 0.4f;
    } else if (e == "winking") {
        s.eye[1].h_scale = 0.12f;
        s.eye[0].bottom = 0.35f;
    } else if (e == "suspicious" || e == "suspicious_eyes") {
        s.eye[0].top = 0.55f;  // one eye squints harder
        s.eye[0].angle = 6.0f;
        s.eye[1].top = 0.32f;
        s.eye[1].angle = 4.0f;
        s.dx = -8;
    } else if (e == "sleepy") {
        both(0.9f, 0.58f, -6.0f, 0.0f);
        s.dy = 4;
    } else if (e == "cool" || e == "confident") {
        both(0.9f, 0.32f, 8.0f, 0.0f);
    } else if (e == "embarrassed") {
        both(0.85f, 0.0f, 0.0f, 0.32f);
        s.dx = -10;
        s.dy = 5;
    } else if (e == "nervous" || e == "eyes_shake") {
        both(0.85f, 0.1f, -8.0f, 0.0f);
        s.w_scale = 0.9f;
    } else if (e == "look_up_left" || e == "nhin tren trai") {
        s.dx = -kLookRangeX;
        s.dy = -kLookRangeY;
    } else if (e == "look_up_right" || e == "nhin tren phai") {
        s.dx = kLookRangeX;
        s.dy = -kLookRangeY;
    } else if (e == "look_down_left" || e == "nhin duoi trai") {
        s.dx = -kLookRangeX;
        s.dy = kLookRangeY;
    } else if (e == "look_down_right" || e == "nhin duoi phai") {
        s.dx = kLookRangeX;
        s.dy = kLookRangeY;
    } else if (e == "look_left") {
        s.dx = -kLookRangeX;
    } else if (e == "look_right") {
        s.dx = kLookRangeX;
    } else {
        *wander = true;  // unknown emotion: calm neutral eyes
    }
    return s;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
void WallEDrawnEyesDisplay::Render() {
    if (layer_ == nullptr) {
        return;
    }
    const int area_h = height_ - kStatusBarHeight;
    for (int i = 0; i < 2; i++) {
        const EyeParams& p = current_.eye[i];
        int w = std::max(4, (int)lroundf(kEyeW * current_.w_scale));
        int h = std::max(2, (int)lroundf(kEyeH * p.h_scale * blink_));
        int cx = kEyeCenterX[i] + (int)lroundf(current_.dx);
        int cy = area_h / 2 + (int)lroundf(current_.dy);

        lv_obj_set_size(eye_[i], w, h);
        lv_obj_set_pos(eye_[i], cx - w / 2, cy - h / 2);
        lv_obj_set_style_radius(eye_[i], std::min(kEyeRadius, h / 2), 0);

        // Upper lid: an "off" rectangle whose bottom edge sits at top*h, tilted around that edge
        if (p.top > 0.01f && h > 4) {
            int lid_w = w + 24;
            int lid_h = h + 4;
            int edge = (int)lroundf(p.top * h);
            lv_obj_set_size(top_lid_[i], lid_w, lid_h);
            lv_obj_set_pos(top_lid_[i], -12, edge - lid_h);
            lv_obj_set_style_transform_pivot_x(top_lid_[i], lid_w / 2, 0);
            lv_obj_set_style_transform_pivot_y(top_lid_[i], lid_h, 0);
            float angle = (i == 0) ? p.angle : -p.angle;  // mirror for the right eye
            lv_obj_set_style_transform_rotation(top_lid_[i], (int32_t)lroundf(angle * 10.0f), 0);
            lv_obj_remove_flag(top_lid_[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(top_lid_[i], LV_OBJ_FLAG_HIDDEN);
        }

        // Lower lid: a wide "off" ellipse rising from below -> happy eye
        if (p.bottom > 0.01f && h > 4) {
            int lid_w = (w * 3) / 2;
            int lid_h = h;
            lv_obj_set_size(bottom_lid_[i], lid_w, lid_h);
            lv_obj_set_pos(bottom_lid_[i], (w - lid_w) / 2, h - (int)lroundf(p.bottom * h));
            lv_obj_remove_flag(bottom_lid_[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(bottom_lid_[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void WallEDrawnEyesDisplay::ShowEyes(bool show) {
    if (layer_ == nullptr) {
        return;
    }
    if (show) {
        lv_obj_remove_flag(layer_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(layer_, LV_OBJ_FLAG_HIDDEN);
    }
}

// ---------------------------------------------------------------------------
// Animation
// ---------------------------------------------------------------------------
void WallEDrawnEyesDisplay::TransitionTo(const Shape& target, uint32_t duration_ms) {
    lv_anim_delete(this, TransitionAnimCb);
    from_ = current_;
    to_ = target;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, this);
    lv_anim_set_exec_cb(&a, TransitionAnimCb);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, duration_ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

void WallEDrawnEyesDisplay::TransitionAnimCb(void* var, int32_t value) {
    auto* self = static_cast<WallEDrawnEyesDisplay*>(var);
    float t = value / 1000.0f;
    Shape& c = self->current_;
    const Shape& f = self->from_;
    const Shape& g = self->to_;
    c.w_scale = Lerp(f.w_scale, g.w_scale, t);
    c.dx = Lerp(f.dx, g.dx, t);
    c.dy = Lerp(f.dy, g.dy, t);
    for (int i = 0; i < 2; i++) {
        c.eye[i].h_scale = Lerp(f.eye[i].h_scale, g.eye[i].h_scale, t);
        c.eye[i].top = Lerp(f.eye[i].top, g.eye[i].top, t);
        c.eye[i].angle = Lerp(f.eye[i].angle, g.eye[i].angle, t);
        c.eye[i].bottom = Lerp(f.eye[i].bottom, g.eye[i].bottom, t);
    }
    self->Render();
}

void WallEDrawnEyesDisplay::BlinkAnimCb(void* var, int32_t value) {
    // value 0..2000: close during the first half, open during the second
    auto* self = static_cast<WallEDrawnEyesDisplay*>(var);
    float t = value <= 1000 ? value / 1000.0f : (2000 - value) / 1000.0f;
    self->blink_ = 1.0f - 0.92f * t;
    self->Render();
}

void WallEDrawnEyesDisplay::BlinkDoneCb(lv_anim_t* a) {
    auto* self = static_cast<WallEDrawnEyesDisplay*>(a->var);
    self->blink_ = 1.0f;
    self->blinking_ = false;
    self->Render();
}

void WallEDrawnEyesDisplay::IdleTimerCb(lv_timer_t* t) {
    auto* self = static_cast<WallEDrawnEyesDisplay*>(lv_timer_get_user_data(t));
    if (self->layer_ == nullptr || lv_obj_has_flag(self->layer_, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    uint32_t now = lv_tick_get();

    if (!self->blinking_ && now >= self->next_blink_ms_) {
        self->blinking_ = true;
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, self);
        lv_anim_set_exec_cb(&a, BlinkAnimCb);
        lv_anim_set_values(&a, 0, 2000);
        lv_anim_set_duration(&a, 160);
        lv_anim_set_completed_cb(&a, BlinkDoneCb);
        lv_anim_start(&a);
        // Occasionally a quick double blink
        self->next_blink_ms_ = now + ((esp_random() % 5 == 0) ? 260 : RandRange(2500, 6000));
    }

    if (self->idle_wander_ && !self->look_override_ && now >= self->next_glance_ms_) {
        static const float kGlances[][2] = {
            {0, 0}, {0, 0}, {-0.6f, 0}, {0.6f, 0}, {-0.4f, -0.6f}, {0.4f, -0.6f}, {0, 0.5f},
        };
        const auto& g = kGlances[esp_random() % (sizeof(kGlances) / sizeof(kGlances[0]))];
        Shape target = self->base_;
        target.dx += g[0] * kLookRangeX;
        target.dy += g[1] * kLookRangeY;
        self->TransitionTo(target, 220);
        self->next_glance_ms_ = now + RandRange(1500, 5000);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
bool WallEDrawnEyesDisplay::SetEmotion(const char* emotion) {
    DisplayLockGuard lock(this);
    if (layer_ == nullptr) {
        return false;
    }
    bool wander = false;
    base_ = ShapeFor(emotion, &wander);
    idle_wander_ = wander;
    Shape target = base_;
    if (look_override_) {
        target.dx += look_x_ * kLookRangeX;
        target.dy += look_y_ * kLookRangeY;
    }
    TransitionTo(target, 180);
    ShowEyes(true);
    return true;
}

void WallEDrawnEyesDisplay::SetChatMessage(const char* role, const char* content) {
    OledDisplay::SetChatMessage(role, content);
    DisplayLockGuard lock(this);
    bool is_system_text =
        role != nullptr && strcmp(role, "system") == 0 && content != nullptr && content[0] != '\0';
    // Wi-Fi setup / activation instructions must be readable
    ShowEyes(!is_system_text);
}

void WallEDrawnEyesDisplay::LookAt(float x, float y) {
    DisplayLockGuard lock(this);
    if (layer_ == nullptr) {
        return;
    }
    look_x_ = std::clamp(x, -1.0f, 1.0f);
    look_y_ = std::clamp(y, -1.0f, 1.0f);
    look_override_ = (look_x_ != 0.0f || look_y_ != 0.0f);
    Shape target = base_;
    target.dx += look_x_ * kLookRangeX;
    target.dy += look_y_ * kLookRangeY;
    TransitionTo(target, 200);
    next_glance_ms_ = lv_tick_get() + 2000;
}

#endif  // WALLE_EYES_STYLE == 0
