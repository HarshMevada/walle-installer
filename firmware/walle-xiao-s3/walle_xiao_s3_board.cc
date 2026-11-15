// Wall-E robot: Seeed XIAO ESP32-S3 Sense + MAX98357 + TB6612FNG + SH1106 OLED
#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "esp32_camera.h"
#include "led/gpio_led.h"
#include "motor_controller.h"
#if WALLE_EYES_STYLE == 1
#include "walle_gif_eyes_display.h"
using WallEEyesDisplay = WallEGifEyesDisplay;
#else
#include "walle_drawn_eyes_display.h"
using WallEEyesDisplay = WallEDrawnEyesDisplay;
#endif

#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_sh1106.h>
#include <esp_log.h>

#define TAG "WallEBoard"

class WallEXiaoS3Board : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;
    WallEEyesDisplay* eyes_ = nullptr;
    Esp32Camera* camera_ = nullptr;
    MotorController motors_;
    Button boot_button_;

    void InitializeDisplayI2c() {
        // I2C port 0. The esp32-camera driver always creates its SCCB bus on
        // I2C port 1 (CONFIG_SCCB_HARDWARE_I2C_PORT1) when it is given pins, and
        // deletes that port's bus if the camera init fails - so the OLED must NOT use port 1.
        i2c_master_bus_config_t bus_config = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
    }

    void InitializeDisplay() {
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = DISPLAY_I2C_ADDR,
            .scl_speed_hz = 400 * 1000,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
        };
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(display_i2c_bus_, &io_config, &panel_io_));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.bits_per_pixel = 1;
        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;
        ESP_ERROR_CHECK(esp_lcd_new_panel_sh1106(panel_io_, &panel_config, &panel_));

        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "OLED not found on I2C (SDA=GPIO%d SCL=GPIO%d) - running without display",
                     DISPLAY_SDA_PIN, DISPLAY_SCL_PIN);
            display_ = new NoDisplay();
            return;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, false));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        eyes_ = new WallEEyesDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                     DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        display_ = eyes_;
    }

    void InitializeCamera() {
        camera_config_t config = {};
        config.pin_pwdn = CAMERA_PIN_PWDN;
        config.pin_reset = CAMERA_PIN_RESET;
        config.pin_xclk = CAMERA_PIN_XCLK;
        config.pin_sccb_sda = CAMERA_PIN_SIOD;
        config.pin_sccb_scl = CAMERA_PIN_SIOC;
        config.sccb_i2c_port = 1;  // informational: the driver uses I2C port 1 for SCCB
        config.pin_d7 = CAMERA_PIN_D7;
        config.pin_d6 = CAMERA_PIN_D6;
        config.pin_d5 = CAMERA_PIN_D5;
        config.pin_d4 = CAMERA_PIN_D4;
        config.pin_d3 = CAMERA_PIN_D3;
        config.pin_d2 = CAMERA_PIN_D2;
        config.pin_d1 = CAMERA_PIN_D1;
        config.pin_d0 = CAMERA_PIN_D0;
        config.pin_vsync = CAMERA_PIN_VSYNC;
        config.pin_href = CAMERA_PIN_HREF;
        config.pin_pclk = CAMERA_PIN_PCLK;
        config.xclk_freq_hz = CAMERA_XCLK_FREQ_HZ;
        config.ledc_timer = LEDC_TIMER_2;
        config.ledc_channel = LEDC_CHANNEL_2;
        config.pixel_format = PIXFORMAT_RGB565;
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_PSRAM;
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
        camera_ = new Esp32Camera(config);
    }

    void InitializeMotors() {
        motors_.Initialize();
        // Wall-E looks where it is going
        motors_.SetMoveCallback([this](MotorController::Dir left, MotorController::Dir right) {
            if (eyes_ == nullptr) {
                return;
            }
            using D = MotorController::Dir;
            if (left == D::kBackward && right == D::kForward) {
                eyes_->LookAt(-1.0f, 0.0f);  // spinning left
            } else if (left == D::kForward && right == D::kBackward) {
                eyes_->LookAt(1.0f, 0.0f);   // spinning right
            } else if (left == D::kStop && right != D::kStop) {
                eyes_->LookAt(-0.6f, 0.0f);  // pivot on left wheel
            } else if (right == D::kStop && left != D::kStop) {
                eyes_->LookAt(0.6f, 0.0f);
            } else if (left == D::kBackward && right == D::kBackward) {
                eyes_->LookAt(0.0f, 0.6f);
            } else {
                eyes_->LookAt(0.0f, 0.0f);
            }
        });
        motors_.RegisterMcpTools();
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

public:
    WallEXiaoS3Board() : boot_button_(BOOT_BUTTON_GPIO) {
        // Motors first so the driver is held in a safe stopped state as early as possible
        InitializeMotors();
        InitializeDisplayI2c();
        InitializeDisplay();
        InitializeCamera();
        InitializeButtons();
        ESP_LOGI(TAG, "Wall-E board initialized");
    }

    Led* GetLed() override {
        static GpioLed led(BUILTIN_LED_GPIO, 1);  // active low
        return &led;
    }

    AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplexPdm audio_codec(
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE, AUDIO_I2S_SPK_GPIO_BCLK,
            AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_PDM_MIC_GPIO_CLK,
            AUDIO_PDM_MIC_GPIO_DATA);
        return &audio_codec;
    }

    Display* GetDisplay() override { return display_; }

    Camera* GetCamera() override { return camera_; }
};

DECLARE_BOARD(WallEXiaoS3Board);
