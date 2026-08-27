/*
 * TB6612FNG + LED Standalone Hardware Self-Test
 * 
 * Tests driver, power (VM/VCC/STBY), and LED wiring with NO WiFi needed.
 * Sequence:
 * 1. Blinks Left LED (Motor A)
 * 2. Blinks Right LED (Motor B)
 * 3. Smoothly fades both LEDs 0% -> 100%
 */

#include <esp_arduino_version.h>

const int PIN_PWMA = 12; // Left Speed
const int PIN_AIN1 = 14; // Left Dir 1
const int PIN_AIN2 = 27; // Left Dir 2

const int PIN_PWMB = 13; // Right Speed
const int PIN_BIN1 = 25; // Right Dir 1
const int PIN_BIN2 = 26; // Right Dir 2

const int PWM_FREQ     = 5000;
const int PWM_RES_BITS = 8;
const int PWM_CHAN_A   = 0;
const int PWM_CHAN_B   = 1;

void writePWM(int pin, int chan, int val) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  ledcWrite(pin, val);
#else
  ledcWrite(chan, val);
#endif
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== TB6612FNG Hardware Self-Test Starting ===");

  pinMode(PIN_AIN1, OUTPUT);
  pinMode(PIN_AIN2, OUTPUT);
  pinMode(PIN_BIN1, OUTPUT);
  pinMode(PIN_BIN2, OUTPUT);

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  ledcAttach(PIN_PWMA, PWM_FREQ, PWM_RES_BITS);
  ledcAttach(PIN_PWMB, PWM_FREQ, PWM_RES_BITS);
#else
  ledcSetup(PWM_CHAN_A, PWM_FREQ, PWM_RES_BITS);
  ledcSetup(PWM_CHAN_B, PWM_FREQ, PWM_RES_BITS);
  ledcAttachPin(PIN_PWMA, PWM_CHAN_A);
  ledcAttachPin(PIN_PWMB, PWM_CHAN_B);
#endif
}

void loop() {
  // Test 1: Left Wheel / LED (Motor A) ON for 1 sec
  Serial.println("Step 1: Testing LEFT Wheel (Green LED)...");
  digitalWrite(PIN_AIN1, HIGH);
  digitalWrite(PIN_AIN2, LOW);
  writePWM(PIN_PWMA, PWM_CHAN_A, 255);
  delay(1000);
  writePWM(PIN_PWMA, PWM_CHAN_A, 0);
  delay(300);

  // Test 2: Right Wheel / LED (Motor B) ON for 1 sec
  Serial.println("Step 2: Testing RIGHT Wheel (Yellow LED)...");
  digitalWrite(PIN_BIN1, HIGH);
  digitalWrite(PIN_BIN2, LOW);
  writePWM(PIN_PWMB, PWM_CHAN_B, 255);
  delay(1000);
  writePWM(PIN_PWMB, PWM_CHAN_B, 0);
  delay(300);

  // Test 3: Smooth Throttle Ramp (0% -> 100%) on BOTH
  Serial.println("Step 3: Testing Smooth PWM Throttle Ramp...");
  digitalWrite(PIN_AIN1, HIGH);
  digitalWrite(PIN_AIN2, LOW);
  digitalWrite(PIN_BIN1, HIGH);
  digitalWrite(PIN_BIN2, LOW);

  for (int duty = 0; duty <= 255; duty += 15) {
    writePWM(PIN_PWMA, PWM_CHAN_A, duty);
    writePWM(PIN_PWMB, PWM_CHAN_B, duty);
    delay(40);
  }
  delay(500);

  writePWM(PIN_PWMA, PWM_CHAN_A, 0);
  writePWM(PIN_PWMB, PWM_CHAN_B, 0);
  delay(1000);
}
