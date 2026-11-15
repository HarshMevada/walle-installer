/*
  Wall-E test 2 - TB6612FNG motors
  --------------------------------
  Board: XIAO_ESP32S3   (Tools > USB CDC On Boot must be "Enabled": D6/D7 are the serial pins)
  Wiring: AIN1->D3  AIN2->D6  BIN1->D7  BIN2->D2  PWMA+PWMB->D9 (joined)
          STBY->3V3  VCC->3V3  VM->motor battery  all GND together

  Lift the robot so the treads spin freely. Open Serial Monitor at 115200.
  The test runs each wheel forward and backward, then both, and repeats.
  PASS = each step in Serial matches what the wheels do.

  Still wired the OLD way (PWMA on D9, PWMB on D5)? Set PWMB_PIN to 6.
*/
#include <Arduino.h>

#if !ARDUINO_USB_CDC_ON_BOOT
#error "Set Tools > USB CDC On Boot > Enabled (D6/D7 are also the serial port pins)"
#endif

#define AIN1 4    // D3
#define AIN2 43   // D6
#define BIN1 44   // D7
#define BIN2 3    // D2
#define PWMA_PIN 8   // D9 (PWMA + PWMB joined)
#define PWMB_PIN -1  // -1 = joined to D9. Old wiring with PWMB on D5 -> 6

#define SPEED_PERCENT 80
#define PWM_FREQ 20000
#define PWM_BITS 8

void setSpeed(int percent) {
  uint32_t duty = (uint32_t)percent * 255 / 100;
  ledcWrite(PWMA_PIN, duty);
  if (PWMB_PIN >= 0) ledcWrite(PWMB_PIN, duty);
}

// dir: 1 forward, -1 backward, 0 stop
void motorA(int dir) { digitalWrite(AIN1, dir > 0); digitalWrite(AIN2, dir < 0); }
void motorB(int dir) { digitalWrite(BIN1, dir > 0); digitalWrite(BIN2, dir < 0); }

void step(const char* what, int a, int b, int ms) {
  Serial.printf("%-34s A=%-8s B=%s\n", what,
                a > 0 ? "forward" : a < 0 ? "backward" : "stop",
                b > 0 ? "forward" : b < 0 ? "backward" : "stop");
  motorA(a);
  motorB(b);
  setSpeed((a || b) ? SPEED_PERCENT : 0);
  delay(ms);
  setSpeed(0);
  motorA(0);
  motorB(0);
  delay(700);
}

void setup() {
  // Keep everything off first
  pinMode(AIN1, OUTPUT); pinMode(AIN2, OUTPUT);
  pinMode(BIN1, OUTPUT); pinMode(BIN2, OUTPUT);
  motorA(0); motorB(0);
  ledcAttach(PWMA_PIN, PWM_FREQ, PWM_BITS);
  if (PWMB_PIN >= 0) ledcAttach(PWMB_PIN, PWM_FREQ, PWM_BITS);
  setSpeed(0);

  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== Wall-E test 2: motors (lift the robot!) ===");
}

void loop() {
  step("1. Motor A (left)  forward", 1, 0, 2000);
  step("2. Motor A (left)  backward", -1, 0, 2000);
  step("3. Motor B (right) forward", 0, 1, 2000);
  step("4. Motor B (right) backward", 0, -1, 2000);
  step("5. Both forward", 1, 1, 2000);
  step("6. Spin left (A back, B fwd)", -1, 1, 1500);
  Serial.println("--- repeating in 4 s ---\n");
  delay(4000);
}
