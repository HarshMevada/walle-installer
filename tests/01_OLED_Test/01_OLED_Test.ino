/*
  Wall-E test 1 - I2C scan + SH1106 OLED
  ---------------------------------------
  Board:   XIAO_ESP32S3      Library: U8g2 (by olikraus)
  Wiring:  OLED SDA -> D4 (GPIO5), OLED SCL -> D10 (GPIO9), VCC -> 3V3, GND -> GND

  Open Serial Monitor at 115200.
  PASS = Serial shows "Found device at 0x3C" and the OLED shows text + blinking eyes.

  If the picture is shifted/garbled with a 2-pixel stripe on one side,
  your module is really an SSD1306: change OLED_IS_SSD1306 to 1.
*/
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

#define OLED_SDA 5   // D4
#define OLED_SCL 9   // D10
#define OLED_IS_SSD1306 0

#if OLED_IS_SSD1306
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);
#else
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);
#endif

uint8_t found_addr = 0;

void scanI2C() {
  Wire.begin(OLED_SDA, OLED_SCL);
  Serial.println("\nScanning I2C bus (SDA=D4, SCL=D10)...");
  int count = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  Found device at 0x%02X\n", a);
      if (a == 0x3C || a == 0x3D) found_addr = a;
      count++;
    }
  }
  if (count == 0) {
    Serial.println("  NOTHING found -> check SDA/SCL wires (maybe swapped), 3V3 and GND.");
  } else if (!found_addr) {
    Serial.println("  Devices found, but no OLED at 0x3C/0x3D.");
  }
  Wire.end();
}

void drawEyes(int dx, int h) {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(0, 9, "OLED OK - Wall-E");
  int y = 38 - h / 2;
  oled.drawRBox(18 + dx, y, 36, h, h > 18 ? 8 : h / 2);
  oled.drawRBox(74 + dx, y, 36, h, h > 18 ? 8 : h / 2);
  oled.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== Wall-E test 1: I2C + OLED ===");
  scanI2C();
  if (found_addr) oled.setI2CAddress(found_addr << 1);
  oled.begin();
  Serial.println("OLED initialised - you should see text and eyes now.");
}

void loop() {
  static const int looks[] = {0, 0, -10, 0, 10, 0};
  for (int dx : looks) {
    drawEyes(dx, 34);
    delay(900);
  }
  for (int h = 34; h >= 4; h -= 10) { drawEyes(0, h); delay(30); }  // blink
  for (int h = 4; h <= 34; h += 10) { drawEyes(0, h); delay(30); }
}
