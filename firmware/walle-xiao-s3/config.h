#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

// =====================================================================
//  Wall-E robot on Seeed XIAO ESP32-S3 Sense
//
//  XIAO pin -> GPIO map (for reference)
//    D0=GPIO1  D1=GPIO2  D2=GPIO3  D3=GPIO4  D4=GPIO5  D5=GPIO6
//    D6=GPIO43 D7=GPIO44 D8=GPIO7  D9=GPIO8  D10=GPIO9
//    D11=GPIO42 / D12=GPIO41 -> on-board PDM microphone (Sense board)
//
//  Wiring used by this firmware
//    D0  GPIO1   -> MAX98357 BCLK
//    D1  GPIO2   -> MAX98357 LRC
//    D5  GPIO6   -> MAX98357 DIN          (moved from D11: GPIO42 is the mic clock)
//    D3  GPIO4   -> TB6612 AIN1
//    D6  GPIO43  -> TB6612 AIN2
//    D7  GPIO44  -> TB6612 BIN1
//    D2  GPIO3   -> TB6612 BIN2
//    D9  GPIO8   -> TB6612 PWMA + PWMB    (both PWM inputs joined, one shared speed)
//    D4  GPIO5   -> SH1106 OLED SDA
//    D10 GPIO9   -> SH1106 OLED SCL
//    D8  GPIO7   -> NOT USED (damaged)
//    TB6612 STBY -> 3.3V
// =====================================================================

#include <driver/gpio.h>

// ---------------- Audio ----------------
#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// Speaker loudness. An automatic gain raises quiet speech up to SPEAKER_BOOST times
// but never pushes peaks past SPEAKER_TARGET_PEAK, so loud parts stay clean.
// 1.0 = original. Lower SPEAKER_TARGET_PEAK (e.g. 24000) if it still sounds harsh.
#define SPEAKER_BOOST            3.0f
#define SPEAKER_TARGET_PEAK      29000
// Microphone gain. The Sense board's PDM mic is very quiet; samples are multiplied
// by this (whole number). Raise to 12-16 if it doesn't wake from across the room,
// lower to 4-6 if it hears itself or keeps triggering.
#define MIC_GAIN                 8
// Volume on first boot (0-100). Later changes ("set volume to 80") are remembered.
#define DEFAULT_OUTPUT_VOLUME    100

// MAX98357 I2S amplifier
#define AUDIO_I2S_SPK_GPIO_BCLK  GPIO_NUM_1   // D0
#define AUDIO_I2S_SPK_GPIO_LRCK  GPIO_NUM_2   // D1
#define AUDIO_I2S_SPK_GPIO_DOUT  GPIO_NUM_6   // D5

// On-board PDM microphone of the XIAO ESP32-S3 Sense (fixed wiring)
#define AUDIO_PDM_MIC_GPIO_CLK   GPIO_NUM_42
#define AUDIO_PDM_MIC_GPIO_DATA  GPIO_NUM_41

// ---------------- Buttons / LED ----------------
#define BOOT_BUTTON_GPIO         GPIO_NUM_0
#define BUILTIN_LED_GPIO         GPIO_NUM_21  // orange user LED, active low

// ---------------- Eye style ----------------
// 0 = drawn Wall-E eyes (live shapes, status bar on top, 20+ expressions)  <- default
// 1 = original Wall-E AI eye animations (full screen, 10 animations, +82 KB flash)
//     Needs walle_eye_frames.c, which is NOT in this public repo (Huy Vector's artwork).
#define WALLE_EYES_STYLE         0

// ---------------- OLED (SH1106 128x64, I2C) ----------------
#define DISPLAY_SDA_PIN          GPIO_NUM_5   // D4
#define DISPLAY_SCL_PIN          GPIO_NUM_9   // D10
#define DISPLAY_I2C_ADDR         0x3C
#define DISPLAY_WIDTH            128
#define DISPLAY_HEIGHT           64
// Flip these if the picture is upside down / mirrored on your mounting
#define DISPLAY_MIRROR_X         false
#define DISPLAY_MIRROR_Y         false

// ---------------- Wi-Fi ----------------
// If the saved Wi-Fi can't be reached at power-up, xiaozhi falls back to the setup
// hotspot and would stay there forever. With a saved network, restart and try again
// after this many seconds in setup mode (unless a phone is connected to the hotspot).
#define WIFI_RETRY_SECONDS       180

// ---------------- Motors (TB6612FNG) ----------------
#define MOTOR_A_IN1_GPIO         GPIO_NUM_4   // D3  AIN1
#define MOTOR_A_IN2_GPIO         GPIO_NUM_43  // D6  AIN2
#define MOTOR_B_IN1_GPIO         GPIO_NUM_44  // D7  BIN1
#define MOTOR_B_IN2_GPIO         GPIO_NUM_3   // D2  BIN2
#define MOTOR_PWM_GPIO           GPIO_NUM_8   // D9  PWMA + PWMB

// Which TB6612 channel is the left wheel (0 = motor A, 1 = motor B)
#define MOTOR_LEFT_IS_A          1
// Set to 1 if a wheel spins the wrong way (easier than swapping wires)
#define MOTOR_LEFT_INVERT        0
#define MOTOR_RIGHT_INVERT       0

#define MOTOR_PWM_FREQ_HZ        20000  // above hearing range, no whine
#define MOTOR_MIN_DUTY_PERCENT   35     // below this small gear motors just hum
#define MOTOR_MAX_DURATION_MS    10000  // safety cap per command

// ---------------- Obstacle sensor (TOF10120, optional) ----------------
// Shares the OLED I2C bus: SDA -> D4, SCL -> D10, VDD -> 3V3, GND -> GND.
// Without it the robot still works; roaming is simply refused.
#define TOF_I2C_ADDR             0x52
#define TOF_I2C_SPEED_HZ         100000
#define OBSTACLE_STOP_MM         200    // forward moves stop closer than this
#define ROAM_AVOID_MM            320    // roaming turns away closer than this
#define ROAM_SPEED               60     // percent
#define ROAM_TURN_MS_MIN         350
#define ROAM_TURN_MS_MAX         900
#define ROAM_MAX_MINUTES         30

// ---------------- Phone web console ----------------
#define WEB_CONSOLE_PORT         80     // control page
#define WEB_STREAM_PORT          81     // live camera (MJPEG)

// ---------------- Camera (XIAO ESP32-S3 Sense, OV3660 / OV2640) ----------------
#define CAMERA_PIN_PWDN          GPIO_NUM_NC
#define CAMERA_PIN_RESET         GPIO_NUM_NC
#define CAMERA_PIN_XCLK          GPIO_NUM_10
#define CAMERA_PIN_SIOD          GPIO_NUM_40
#define CAMERA_PIN_SIOC          GPIO_NUM_39
#define CAMERA_PIN_D7            GPIO_NUM_48
#define CAMERA_PIN_D6            GPIO_NUM_11
#define CAMERA_PIN_D5            GPIO_NUM_12
#define CAMERA_PIN_D4            GPIO_NUM_14
#define CAMERA_PIN_D3            GPIO_NUM_16
#define CAMERA_PIN_D2            GPIO_NUM_18
#define CAMERA_PIN_D1            GPIO_NUM_17
#define CAMERA_PIN_D0            GPIO_NUM_15
#define CAMERA_PIN_VSYNC         GPIO_NUM_38
#define CAMERA_PIN_HREF          GPIO_NUM_47
#define CAMERA_PIN_PCLK          GPIO_NUM_13
#define CAMERA_XCLK_FREQ_HZ      20000000

#endif // _BOARD_CONFIG_H_
