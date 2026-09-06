/*
 * Unified ESP32 Robot Firmware:
 * 1. Drives Left & Right GA25 Motors via TB6612FNG (Core 2.x & 3.x compatible)
 * 2. Reads VL53L0X Laser Distance Sensor via I2C (Pins 21/22) with 8191 filter
 * 3. Bidirectional UDP Telemetry with Laptop ROS 2
 * 
 * Hardware Wiring:
 * 1. TB6612FNG Driver to ESP32:
 *    - VCC  -> ESP32 3V3
 *    - STBY -> ESP32 3V3
 *    - GND  -> ESP32 GND (and Battery Ground)
 *    - VM   -> 7.4V Battery (+)
 *    
 *    - PWMA (Left Speed)  -> GPIO 12 (D12)
 *    - AIN1 (Left Dir 1)  -> GPIO 14 (D14)
 *    - AIN2 (Left Dir 2)  -> GPIO 27 (D27)
 *    
 *    - PWMB (Right Speed) -> GPIO 13 (D13)
 *    - BIN1 (Right Dir 1) -> GPIO 25 (D25)
 *    - BIN2 (Right Dir 2) -> GPIO 26 (D26)
 * 
 * 2. GY-VL53L0X Laser Sensor to ESP32:
 *    - VCC -> ESP32 3V3
 *    - GND -> ESP32 GND
 *    - SDA -> GPIO 21 (D21)
 *    - SCL -> GPIO 22 (D22)
 * 
 * 3. Motor Outputs:
 *    - A01 & A02 -> Left Motor
 *    - B01 & B02 -> Right Motor
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include "Adafruit_VL53L0X.h"
#include <esp_arduino_version.h>

// WiFi Configuration
const char* ssid      = "Brajesh_2.4GHz";
const char* password  = "Ash@0812#@";
const char* laptop_ip = "192.168.29.131"; // Laptop IP
const int   udp_port   = 8889;             // Motor velocity commands IN
const int   laser_port = 8890;            // Laser distance stream OUT

WiFiUDP udp;
Adafruit_VL53L0X lox = Adafruit_VL53L0X();
bool laser_ok = false;

// TB6612FNG Pin Definitions
const int PIN_PWMA = 12; // Left Speed
const int PIN_AIN1 = 14; // Left Dir 1
const int PIN_AIN2 = 27; // Left Dir 2

const int PIN_PWMB = 13; // Right Speed
const int PIN_BIN1 = 25; // Right Dir 1
const int PIN_BIN2 = 26; // Right Dir 2

// PWM Configuration
const int PWM_FREQ     = 5000;
const int PWM_RES_BITS = 8;    // 0 - 255
const int PWM_CHAN_A   = 0;
const int PWM_CHAN_B   = 1;

unsigned long lastPacketTime    = 0;
unsigned long lastLaserScanTime = 0;
const unsigned long TIMEOUT_MS  = 500; // Auto-brake if no signal for 0.5s

void setMotors(int left_pwm, int right_pwm) {
  // Left Motor Control
  int speed_a = constrain(abs(left_pwm), 0, 255);
  if (left_pwm > 0) {
    digitalWrite(PIN_AIN1, HIGH);
    digitalWrite(PIN_AIN2, LOW);
  } else if (left_pwm < 0) {
    digitalWrite(PIN_AIN1, LOW);
    digitalWrite(PIN_AIN2, HIGH);
  } else {
    digitalWrite(PIN_AIN1, LOW);
    digitalWrite(PIN_AIN2, LOW);
  }

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  ledcWrite(PIN_PWMA, speed_a);
#else
  ledcWrite(PWM_CHAN_A, speed_a);
#endif

  // Right Motor Control
  int speed_b = constrain(abs(right_pwm), 0, 255);
  if (right_pwm > 0) {
    digitalWrite(PIN_BIN1, HIGH);
    digitalWrite(PIN_BIN2, LOW);
  } else if (right_pwm < 0) {
    digitalWrite(PIN_BIN1, LOW);
    digitalWrite(PIN_BIN2, HIGH);
  } else {
    digitalWrite(PIN_BIN1, LOW);
    digitalWrite(PIN_BIN2, LOW);
  }

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  ledcWrite(PIN_PWMB, speed_b);
#else
  ledcWrite(PWM_CHAN_B, speed_b);
#endif
}

void setup() {
  Serial.begin(115200);
  delay(1000); // Power stabilization

  Serial.println("\n--- Booting Unified Robot ESP32 ---");

  // Motor GPIO Setup
  pinMode(PIN_AIN1, OUTPUT);
  pinMode(PIN_AIN2, OUTPUT);
  pinMode(PIN_BIN1, OUTPUT);
  pinMode(PIN_BIN2, OUTPUT);

  // Universal PWM Setup (Core 2.x & 3.x)
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  ledcAttach(PIN_PWMA, PWM_FREQ, PWM_RES_BITS);
  ledcAttach(PIN_PWMB, PWM_FREQ, PWM_RES_BITS);
#else
  ledcSetup(PWM_CHAN_A, PWM_FREQ, PWM_RES_BITS);
  ledcSetup(PWM_CHAN_B, PWM_FREQ, PWM_RES_BITS);
  ledcAttachPin(PIN_PWMA, PWM_CHAN_A);
  ledcAttachPin(PIN_PWMB, PWM_CHAN_B);
#endif

  setMotors(0, 0);

  // 1. Wi-Fi Connection
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(ssid);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(300);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi Connected! Robot IP: " + WiFi.localIP().toString());
    udp.begin(udp_port);
  } else {
    Serial.println("\n❌ WiFi Failed! Check router connection.");
  }

  // 2. I2C Laser Sensor Setup
  Wire.begin(21, 22);
  Wire.setTimeOut(100);
  if (lox.begin()) {
    laser_ok = true;
    Serial.println("✅ VL53L0X Laser Sensor Online!");
  } else {
    Serial.println("⚠️ Warning: VL53L0X not found on pins 21/22. Motors will still work!");
  }
}

void loop() {
  // 1. Process Motor Velocity Commands from Remote / ROS 2
  int packetSize = udp.parsePacket();
  if (packetSize) {
    char packetBuffer[255];
    int len = udp.read(packetBuffer, 254);
    if (len > 0) packetBuffer[len] = 0;
    
    int left_pwm = 0, right_pwm = 0;
    if (sscanf(packetBuffer, "MOTOR:%d,%d", &left_pwm, &right_pwm) == 2) {
      setMotors(left_pwm, right_pwm);
      lastPacketTime = millis();
    }
  }

  // Auto-Brake safety timeout
  if (millis() - lastPacketTime > TIMEOUT_MS) {
    setMotors(0, 0);
  }

  // 2. Read & Filter Laser Sensor at 20Hz (every 50ms)
  if (laser_ok && (millis() - lastLaserScanTime > 50)) {
    lastLaserScanTime = millis();
    VL53L0X_RangingMeasurementData_t measure;
    lox.rangingTest(&measure, false);

    // Filter out 8191 out-of-range flag and noise
    if (measure.RangeStatus == 0 && measure.RangeMilliMeter < 2200 && measure.RangeMilliMeter > 20) {
      float dist_m = measure.RangeMilliMeter / 1000.0;
      String packet = "DIST:" + String(dist_m, 3);
      udp.beginPacket(laptop_ip, laser_port);
      udp.print(packet);
      udp.endPacket();
    }
  }
}
