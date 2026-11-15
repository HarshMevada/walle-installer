#pragma once

#include "display/oled_display.h"
#include "walle_eye_frames.h"

#include <lvgl.h>

// Full-screen Wall-E eyes on a 128x64 monochrome OLED, using the original
// Wall-E AI eye animations (converted to 1-bit by tools/gif_to_oled.py).
//
// Every original animation starts and ends on the normal open eyes, so they are
// played as one-shot moves: an emotion plays its animation, and while idle the
// robot blinks and glances around at random, like the original.
// System messages (Wi-Fi setup, activation code) temporarily replace the eyes.
class WallEGifEyesDisplay : public OledDisplay {
public:
    WallEGifEyesDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                     int height, bool mirror_x, bool mirror_y);
    ~WallEGifEyesDisplay();

    void SetupUI() override;
    bool SetEmotion(const char* emotion) override;
    void SetChatMessage(const char* role, const char* content) override;

    // Glance in a direction while driving: x<0 left, x>0 right, y>0 down.
    void LookAt(float x, float y);

private:
    lv_obj_t* layer_ = nullptr;
    lv_obj_t* canvas_ = nullptr;
    uint16_t* canvas_buf_ = nullptr;
    lv_timer_t* timer_ = nullptr;

    const walle_eye_anim_t* anim_ = nullptr;  // animation being played (nullptr = idle)
    int frame_ = 0;
    uint32_t next_frame_ms_ = 0;
    uint32_t next_idle_ms_ = 0;
    bool emotion_playing_ = false;  // an emotion animation is running (don't interrupt with idle)

    static const walle_eye_anim_t* FindAnim(const char* name);
    static const char* AnimForEmotion(const char* emotion);

    void Play(const walle_eye_anim_t* anim, bool is_emotion);
    void DrawFrame(uint16_t frame_index);
    void ShowEyes(bool show);
    void ScheduleIdle(uint32_t now);

    static void TimerCb(lv_timer_t* t);
};
