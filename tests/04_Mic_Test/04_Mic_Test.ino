/*
  Wall-E test 4 - built-in microphone (XIAO ESP32-S3 Sense), diagnostic version
  ---------------------------------------------------------------------------
  Board: XIAO_ESP32S3 (Tools > PSRAM: "OPI PSRAM")     Library: U8g2
  Needs the Sense expansion board (camera + mic board) pressed firmly onto the XIAO.
  The mic uses GPIO42 (clock) and GPIO41 (data): NOTHING may be wired to D11 / D12.

  What it does
   1. AUTO-CHECK: tries the 4 possible ways to read the mic and reports which one gives sound.
   2. LIVE METER: shows the sound level on the OLED (big bar) and in Logs & Console.
   3. RECORD + PLAY: press BOOT, speak for 3 s -> Wall-E plays your voice on the speaker.

  PASS = auto-check says "MIC OK", the bar jumps when you talk/clap, and you hear yourself back.
*/
#include <Arduino.h>
#include <U8g2lib.h>
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "esp_heap_caps.h"

// ---- pins ----
#define MIC_CLK   GPIO_NUM_42
#define MIC_DATA  GPIO_NUM_41
#define AMP_BCLK  GPIO_NUM_1   // D0
#define AMP_LRC   GPIO_NUM_2   // D1
#define AMP_DIN   GPIO_NUM_6   // D5
#define OLED_SDA  5            // D4
#define OLED_SCL  9            // D10
#define BOOT_BTN  0

#define RATE      16000
#define REC_SECONDS 3

U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);

void oledText(const char* a, const char* b);
void oledMeter(int level);

i2s_chan_handle_t mic_chan = nullptr;
i2s_chan_handle_t spk_chan = nullptr;

struct MicMode { i2s_pdm_slot_mask_t slot; bool clk_inv; const char* name; };
const MicMode kModes[] = {
  {I2S_PDM_SLOT_LEFT,  false, "left slot"},
  {I2S_PDM_SLOT_RIGHT, false, "right slot"},
  {I2S_PDM_SLOT_LEFT,  true,  "left slot, clock inverted"},
  {I2S_PDM_SLOT_RIGHT, true,  "right slot, clock inverted"},
};
int best_mode = -1;

struct Stats { int n; int zeros; int minv; int maxv; int rms; };

// ---------------------------------------------------------------- mic
bool micStart(const MicMode& m) {
  if (mic_chan) {
    i2s_channel_disable(mic_chan);
    i2s_del_channel(mic_chan);
    mic_chan = nullptr;
  }
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);  // PDM RX only on I2S0
  if (i2s_new_channel(&chan_cfg, nullptr, &mic_chan) != ESP_OK) return false;
  i2s_pdm_rx_config_t cfg = {
    .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(RATE),
    .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
      .clk = MIC_CLK,
      .din = MIC_DATA,
      .invert_flags = { .clk_inv = m.clk_inv },
    },
  };
  cfg.slot_cfg.slot_mask = m.slot;
  if (i2s_channel_init_pdm_rx_mode(mic_chan, &cfg) != ESP_OK) return false;
  return i2s_channel_enable(mic_chan) == ESP_OK;
}

size_t micRead(int16_t* buf, size_t samples) {
  size_t got = 0;
  if (i2s_channel_read(mic_chan, buf, samples * 2, &got, 200) != ESP_OK) return 0;
  return got / 2;
}

Stats analyse(const int16_t* b, int n) {
  Stats s = {n, 0, 32767, -32768, 0};
  long long sum = 0;
  for (int i = 0; i < n; i++) sum += b[i];
  int dc = n ? sum / n : 0;
  double sq = 0;
  for (int i = 0; i < n; i++) {
    if (b[i] == 0) s.zeros++;
    s.minv = min(s.minv, (int)b[i]);
    s.maxv = max(s.maxv, (int)b[i]);
    double v = b[i] - dc;
    sq += v * v;
  }
  s.rms = n ? (int)sqrt(sq / n) : 0;
  return s;
}

void autoCheck() {
  static int16_t buf[4000];  // 250 ms
  Serial.println("\nAUTO-CHECK: trying all 4 mic settings (make some noise / keep talking)...");
  oledText("AUTO-CHECK", "talk or clap now...");
  int best_rms = -1;
  for (int i = 0; i < 4; i++) {
    if (!micStart(kModes[i])) {
      Serial.printf("  [%d] %-28s -> could not start I2S\n", i + 1, kModes[i].name);
      continue;
    }
    micRead(buf, 4000);  // throw away the first 250 ms (filter settling)
    int n = micRead(buf, 4000);
    Stats s = analyse(buf, n);
    const char* verdict;
    if (n == 0) verdict = "NO DATA (read timeout)";
    else if (s.zeros > n * 95 / 100) verdict = "all zeros";
    else if (s.maxv == s.minv) verdict = "stuck value";
    else if (s.rms < 3) verdict = "almost silent";
    else verdict = "sound OK";
    Serial.printf("  [%d] %-28s -> samples=%d zeros=%d min=%d max=%d level=%d  => %s\n",
                  i + 1, kModes[i].name, n, s.zeros, s.minv, s.maxv, s.rms, verdict);
    bool usable = n > 0 && s.zeros <= n * 95 / 100 && s.maxv != s.minv;
    if (usable && s.rms > best_rms) { best_rms = s.rms; best_mode = i; }
  }
  if (best_mode < 0) {
    Serial.println("\nRESULT: MIC FAIL - no setting gives any data.");
    Serial.println("  -> Press the Sense expansion board firmly onto the XIAO (B2B connector).");
    Serial.println("  -> Make sure NOTHING is wired to D11 (GPIO42) or D12 (GPIO41).");
    Serial.println("  -> Try another USB port / cable, then press RESET.");
    oledText("MIC FAIL", "see Logs & Console");
  } else {
    Serial.printf("\nRESULT: MIC OK using \"%s\". Live meter starts now.\n", kModes[best_mode].name);
    Serial.println("Press BOOT to record 3 s and play it back on the speaker.\n");
    micStart(kModes[best_mode]);
  }
}

// ---------------------------------------------------------------- speaker
bool speakerStart() {
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
  chan_cfg.auto_clear = true;
  if (i2s_new_channel(&chan_cfg, &spk_chan, nullptr) != ESP_OK) return false;
  i2s_std_config_t cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = AMP_BCLK,
      .ws = AMP_LRC,
      .dout = AMP_DIN,
      .din = I2S_GPIO_UNUSED,
      .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
    },
  };
  if (i2s_channel_init_std_mode(spk_chan, &cfg) != ESP_OK) return false;
  return i2s_channel_enable(spk_chan) == ESP_OK;
}

void recordAndPlay() {
  const int total = RATE * REC_SECONDS;
  int16_t* rec = (int16_t*)heap_caps_malloc(total * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!rec) rec = (int16_t*)malloc(total * 2);
  if (!rec) { Serial.println("No memory for recording"); return; }

  Serial.println("RECORDING 3 s - speak now!");
  oledText("RECORDING...", "speak now!");
  int got = 0;
  while (got < total) {
    int n = micRead(rec + got, min(1024, total - got));
    if (n == 0) break;
    got += n;
  }

  // remove DC offset and boost to a comfortable volume
  long long sum = 0;
  for (int i = 0; i < got; i++) sum += rec[i];
  int dc = got ? sum / got : 0;
  int peak = 1;
  for (int i = 0; i < got; i++) { rec[i] -= dc; peak = max(peak, abs((int)rec[i])); }
  float gain = min(30.0f, 20000.0f / peak);
  for (int i = 0; i < got; i++) {
    int v = (int)(rec[i] * gain);
    rec[i] = (int16_t)constrain(v, -32767, 32767);
  }
  Serial.printf("Recorded %d samples, peak=%d, playback gain x%.1f\n", got, peak, gain);

  if (!spk_chan) {
    Serial.println("Speaker could not start - skipping playback (run test 3).");
  } else {
    Serial.println("PLAYING BACK...");
    oledText("PLAYING BACK", "listen...");
    size_t written;
    i2s_channel_write(spk_chan, rec, got * 2, &written, 5000);
    int16_t silence[512] = {0};
    i2s_channel_write(spk_chan, silence, sizeof(silence), &written, 500);
    Serial.println("Done. Did you hear yourself? Press BOOT to try again.\n");
  }
  free(rec);
}

// ---------------------------------------------------------------- OLED
bool oled_ok = false;

void oledText(const char* a, const char* b) {
  if (!oled_ok) return;
  oled.clearBuffer();
  oled.setFont(u8g2_font_7x14B_tf);
  oled.drawStr(0, 16, a);
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(0, 34, b);
  oled.sendBuffer();
}

void oledMeter(int level) {
  if (!oled_ok) return;
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(0, 9, "MIC OK - talk/clap");
  oled.drawStr(0, 62, "BOOT = record+play");
  int w = constrain(level * 124 / 1500, 0, 124);
  oled.drawFrame(0, 18, 128, 30);
  oled.drawBox(2, 20, w, 26);
  oled.sendBuffer();
}

// ---------------------------------------------------------------- main
void setup() {
  pinMode(BOOT_BTN, INPUT_PULLUP);
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== Wall-E test 4: microphone (diagnostic) ===");

  oled_ok = oled.begin();
  oledText("MIC TEST", "starting...");

  if (!speakerStart()) Serial.println("(speaker I2S did not start - playback disabled)");
  autoCheck();
}

void loop() {
  if (best_mode < 0) {  // failed: repeat the check every 10 s
    delay(10000);
    autoCheck();
    return;
  }

  static int16_t buf[1600];  // 100 ms
  int n = micRead(buf, 1600);
  Stats s = analyse(buf, n);

  int bars = constrain(s.rms / 30, 0, 50);
  char bar[52];
  memset(bar, '#', bars);
  bar[bars] = 0;
  Serial.printf("level:%5d  %s\n", s.rms, bar);
  oledMeter(s.rms);

  if (digitalRead(BOOT_BTN) == LOW) {
    delay(50);
    while (digitalRead(BOOT_BTN) == LOW) delay(10);
    recordAndPlay();
  }
}
