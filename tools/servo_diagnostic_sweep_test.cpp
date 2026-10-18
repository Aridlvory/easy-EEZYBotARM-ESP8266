/*
  DIAGNOSTIC ONLY - minimal PCA9685 servo sweep test
  ----------------------------------------------------
  Purpose: isolate whether the PCA9685 -> servo signal path physically
  works at all, with ZERO other code in the way (no WiFi, no web server,
  no kinematics, no mapping functions).

  HOW TO USE:
  1. Rename your current src/main.cpp to src/main_full.cpp.bak (keep it safe)
  2. Rename this file to src/main.cpp
  3. Upload and open the Serial Monitor (115200 baud)
  4. Watch BOTH the serial output AND the physical servo on channel 0
  5. When done testing, reverse steps 1-2 to restore your real firmware

  WHAT TO WATCH FOR:
  - If the servo on channel 0 sweeps smoothly from one end to the other
    and back, the whole signal chain (I2C, PCA9685, wiring, that servo)
    is confirmed working - the problem must be somewhere in the full
    project's software logic instead.
  - If it does NOT move here either, the problem is 100% in the
    hardware signal path (PCA9685 channel 0 output, or that specific
    servo, or the wire between them) - not in any of the WiFi/kinematics
    code, since none of that exists in this file at all.
*/

#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);

#define SERVO_FREQ 50
#define SERVOMIN 150
#define SERVOMAX 600
#define TEST_CHANNEL 0   // change to 1, 2, or 3 to test other channels/servos

int angleToPulse(int deg) {
  deg = constrain(deg, 0, 180);
  return map(deg, 0, 180, SERVOMIN, SERVOMAX);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=== PCA9685 minimal servo sweep test ===");

  Wire.begin(D2, D1);

  Serial.println("[I2C] Scanning...");
  bool found = false;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[I2C] Found device at 0x%02X\n", addr);
      if (addr == 0x40) found = true;
    }
  }
  if (!found) {
    Serial.println("[I2C] PCA9685 NOT found at 0x40 - stopping here, this IS the problem.");
    while (true) { delay(1000); }
  }

  pwm.begin();
  pwm.setPWMFreq(SERVO_FREQ);
  delay(10);
  Serial.printf("[TEST] Sweeping channel %d from 0 to 180 and back, forever.\n", TEST_CHANNEL);
  Serial.println("[TEST] Watch the physical servo now.");
}

void loop() {
  for (int deg = 0; deg <= 180; deg += 10) {
    int pulse = angleToPulse(deg);
    Serial.printf("[SWEEP] deg=%3d  pulse=%3d\n", deg, pulse);
    pwm.setPWM(TEST_CHANNEL, 0, pulse);
    delay(400); // slow, so you can actually watch it move step by step
  }
  for (int deg = 180; deg >= 0; deg -= 10) {
    int pulse = angleToPulse(deg);
    Serial.printf("[SWEEP] deg=%3d  pulse=%3d\n", deg, pulse);
    pwm.setPWM(TEST_CHANNEL, 0, pulse);
    delay(400);
  }
}
