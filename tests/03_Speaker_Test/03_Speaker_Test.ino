/*
  Wall-E test 3 - MAX98357 speaker
  --------------------------------
  Board: XIAO_ESP32S3
  Wiring: BCLK->D0  LRC->D1  DIN->D5  VIN->5V  GND->GND  speaker on + / -
          (SD and GAIN pins left unconnected)

  Open Serial Monitor at 115200.
  PASS = you hear three beeps (low, mid, high) and then a short melody, repeating.

  Still wired the OLD way (DIN on D11)? Set AMP_DIN to 42 just for this test.
*/
#include <Arduino.h>
#include <ESP_I2S.h>

#define AMP_BCLK 1   // D0
#define AMP_LRC  2   // D1
#define AMP_DIN  6   // D5

#define SAMPLE_RATE 16000
#define VOLUME 0.25f  // 0..1, keep low for small speakers

I2SClass i2s;

void tone_ms(float freq, int ms) {
  const int n = 256;
  int16_t buf[n];
  static float phase = 0;
  int total = SAMPLE_RATE * ms / 1000;
  while (total > 0) {
    int count = total < n ? total : n;
    for (int i = 0; i < count; i++) {
      buf[i] = freq > 0 ? (int16_t)(sinf(phase) * 32767 * VOLUME) : 0;
      phase += 2 * PI * freq / SAMPLE_RATE;
      if (phase > 2 * PI) phase -= 2 * PI;
    }
    i2s.write((uint8_t*)buf, count * sizeof(int16_t));
    total -= count;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== Wall-E test 3: speaker ===");
  i2s.setPins(AMP_BCLK, AMP_LRC, AMP_DIN);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("I2S start FAILED");
    while (true) delay(1000);
  }
  Serial.println("I2S started - listen to the speaker.");
}

void loop() {
  Serial.println("Beep 400 Hz");  tone_ms(400, 500);  tone_ms(0, 300);
  Serial.println("Beep 800 Hz");  tone_ms(800, 500);  tone_ms(0, 300);
  Serial.println("Beep 1600 Hz"); tone_ms(1600, 500); tone_ms(0, 500);
  Serial.println("Melody");
  const float notes[] = {523, 659, 784, 1047, 784, 1047};
  for (float f : notes) { tone_ms(f, 180); tone_ms(0, 40); }
  tone_ms(0, 1500);
}
