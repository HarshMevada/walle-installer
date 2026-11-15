/*
  Wall-E test 4 - built-in microphone (XIAO ESP32-S3 Sense)
  ---------------------------------------------------------
  Board: XIAO_ESP32S3. Needs the Sense expansion board (camera/mic board) attached.
  The mic uses GPIO42 (clock) and GPIO41 (data): nothing else may be wired to D11/D12.

  Open Serial Monitor (or Tools > Serial Plotter) at 115200.
  PASS = the level bar is small when quiet and jumps when you talk or clap.
*/
#include <Arduino.h>
#include <ESP_I2S.h>

#define MIC_CLK  42
#define MIC_DATA 41
#define SAMPLE_RATE 16000

I2SClass mic;

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== Wall-E test 4: microphone ===");
  mic.setPinsPdmRx(MIC_CLK, MIC_DATA);
  if (!mic.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("Mic start FAILED (is the Sense expansion board attached?)");
    while (true) delay(1000);
  }
  Serial.println("Mic started - talk or clap near the board.");
}

void loop() {
  int16_t buf[800];  // 50 ms of audio
  size_t bytes = mic.readBytes((char*)buf, sizeof(buf));
  int n = bytes / 2;
  if (n == 0) return;

  double sum = 0;
  long dc = 0;
  for (int i = 0; i < n; i++) dc += buf[i];
  dc /= n;
  for (int i = 0; i < n; i++) {
    double v = buf[i] - dc;
    sum += v * v;
  }
  int rms = (int)sqrt(sum / n);

  int bars = min(50, rms / 40);
  char bar[52];
  memset(bar, '#', bars);
  bar[bars] = 0;
  Serial.printf("level:%d\t%s\n", rms, bar);
}
