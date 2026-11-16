#pragma once

#include "display/oled_display.h"

#include <lvgl.h>

#include <string>

// Wall-E style animated eyes drawn live on a 128x64 monochrome OLED (WALLE_EYES_STYLE 0).
//
// The normal xiaozhi OLED status bar stays on top; under it two big rounded eyes
// blink, glance around, change shape with the AI's emotion and look where Wall-E
// is turning. System messages (Wi-Fi setup, activation code) temporarily hide the eyes.
class WallEDrawnEyesDisplay : public OledDisplay {
public:
    WallEDrawnEyesDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                          int width, int height, bool mirror_x, bool mirror_y);
    ~WallEDrawnEyesDisplay();

    void SetupUI() override;
    bool SetEmotion(const char* emotion) override;
    void SetChatMessage(const char* role, const char* content) override;

    // Look in a direction, -1..1 on each axis (0,0 = straight ahead).
    void LookAt(float x, float y);

    // Show a text message (emoji supported) over the eyes for a few seconds.
    void ShowMessage(const std::string& text, int seconds);

private:
    struct EyeParams {
        float h_scale = 1.0f;  // eye height multiplier
        float top = 0.0f;      // fraction covered by the upper lid (0..1)
        float angle = 0.0f;    // upper lid tilt in degrees, + = inner corner down (angry)
        float bottom = 0.0f;   // fraction covered by the round lower lid (happy eyes)
    };
    struct Shape {
        float w_scale = 1.0f;
        float dx = 0.0f;  // gaze offset in pixels
        float dy = 0.0f;
        EyeParams eye[2];  // 0 = left, 1 = right
    };

    lv_obj_t* layer_ = nullptr;
    lv_obj_t* eye_[2] = {nullptr, nullptr};
    lv_obj_t* top_lid_[2] = {nullptr, nullptr};
    lv_obj_t* bottom_lid_[2] = {nullptr, nullptr};
    lv_timer_t* idle_timer_ = nullptr;

    // text + emoji
    lv_font_t emoji_font_ = {};
    lv_font_t text_font_ = {};
    lv_obj_t* msg_layer_ = nullptr;
    lv_obj_t* msg_label_ = nullptr;
    lv_timer_t* msg_timer_ = nullptr;

    // extra expressions
    enum class Fx { kNone, kBounce, kShake, kRoll };
    Fx fx_ = Fx::kNone;
    float fx_dx_ = 0.0f;
    float fx_dy_ = 0.0f;
    lv_obj_t* heart_[2] = {nullptr, nullptr};
    lv_timer_t* heart_timer_ = nullptr;
    lv_obj_t* zzz_ = nullptr;
    bool sleeping_ = false;
    int zzz_step_ = 0;

    Shape base_;  // shape of the current emotion, without glances
    Shape from_;
    Shape to_;
    Shape current_;
    float blink_ = 1.0f;  // 1 = open, ~0 = closed
    bool blinking_ = false;
    bool idle_wander_ = true;
    bool look_override_ = false;
    float look_x_ = 0.0f;
    float look_y_ = 0.0f;
    uint32_t next_blink_ms_ = 0;
    uint32_t next_glance_ms_ = 0;

    static Shape ShapeFor(const char* emotion, bool* wander);
    void TransitionTo(const Shape& target, uint32_t duration_ms);
    void Render();
    void ShowEyes(bool show);

    static void TransitionAnimCb(void* var, int32_t value);
    static void BlinkAnimCb(void* var, int32_t value);
    static void BlinkDoneCb(lv_anim_t* a);
    static void IdleTimerCb(lv_timer_t* t);

    void StartFx(Fx fx, uint32_t duration_ms);
    void ShowHearts(uint32_t duration_ms);
    void HideHearts();
    void SetSleeping(bool sleeping);
    void HideMessage();
    static void FxAnimCb(void* var, int32_t value);
    static void FxDoneCb(lv_anim_t* a);
    static void HeartPulseCb(void* var, int32_t value);
    static void MsgScrollCb(void* var, int32_t value);
};
