/*
 * Wireless ESP32 Robot Receiver & TB6612FNG Motor Driver Controller
 * 
 * Hardware Wiring:
 * 1. TB6612FNG Driver to ESP32 #2:
 *    - VCC  -> ESP32 3V3
 *    - STBY -> ESP32 3V3
 *    - GND  -> ESP32 GND
 *    - VM   -> 3.5V-4.2V Battery (+) or ESP32 VIN
 *    
 *    - PWMA (Left Speed)  -> GPIO 12 (D12)
 *    - AIN1 (Left Dir 1)  -> GPIO 14 (D14)
 *    - AIN2 (Left Dir 2)  -> GPIO 27 (D27)
 *    
 *    - PWMB (Right Speed) -> GPIO 13 (D13)
 *    - BIN1 (Right Dir 1) -> GPIO 25 (D25)
 *    - BIN2 (Right Dir 2) -> GPIO 26 (D26)
 * 
 * 2. Motor / LED Outputs:
 *    - A01 & A02 -> Left LED + 220 Ohm Resistor (or Left Motor)
 *    - B01 & B02 -> Right LED + 220 Ohm Resistor (or Right Motor)
 */

#include <WiFi.h>
#include <WiFiUdp.h>

// WiFi Configuration - Must match your home WiFi or Laptop Hotspot
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// UDP Port to receive motor velocity commands from Laptop ROS 2
const int udp_port = 8889;
WiFiUDP udp;

// TB6612FNG Pin Definitions
const int PIN_PWMA = 12; // Left Speed
const int PIN_AIN1 = 14; // Left Dir 1
const int PIN_AIN2 = 27; // Left Dir 2

const int PIN_PWMB = 13; // Right Speed
const int PIN_BIN1 = 25; // Right Dir 1
const int PIN_BIN2 = 26; // Right Dir 2

// PWM Channels on ESP32
const int PWM_FREQ     = 5000;
const int PWM_RES_BITS = 8;    // 0 - 255
const int PWM_CHAN_A   = 0;
const int PWM_CHAN_B   = 1;

// Robot Kinematics
const float WHEEL_BASE   = 0.35; // 0.35m track width
const float WHEEL_RADIUS = 0.05; // 0.05m radius

unsigned long lastPacketTime = 0;
const unsigned long TIMEOUT_MS = 500; // Auto-brake if WiFi lost for 0.5s

void setup() {
  Serial.begin(115200);

  // Direction Pins
  pinMode(PIN_AIN1, OUTPUT);
  pinMode(PIN_AIN2, OUTPUT);
  pinMode(PIN_BIN1, OUTPUT);
  pinMode(PIN_BIN2, OUTPUT);

  // Configure PWM Channels on ESP32
  ledcSetup(PWM_CHAN_A, PWM_FREQ, PWM_RES_BITS);
  ledcSetup(PWM_CHAN_B, PWM_FREQ, PWM_RES_BITS);
  ledcAttachPin(PIN_PWMA, PWM_CHAN_A);
  ledcAttachPin(PIN_PWMB, PWM_CHAN_B);

  // Initial State: Stopped
  stopMotors();

  // Connect to Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected! Robot IP: ");
  Serial.println(WiFi.localIP());

  udp.begin(udp_port);
  Serial.printf("Listening for velocity commands on UDP port %d\n", udp_port);
}

void setMotorLeft(int speed, bool forward) {
  speed = constrain(abs(speed), 0, 255);
  if (speed == 0) {
    digitalWrite(PIN_AIN1, LOW);
    digitalWrite(PIN_AIN2, LOW);
  } else if (forward) {
    digitalWrite(PIN_AIN1, HIGH);
    digitalWrite(PIN_AIN2, LOW);
  } else {
    digitalWrite(PIN_AIN1, LOW);
    digitalWrite(PIN_AIN2, HIGH);
  }
  ledcWrite(PWM_CHAN_A, speed);
}

void setMotorRight(int speed, bool forward) {
  speed = constrain(abs(speed), 0, 255);
  if (speed == 0) {
    digitalWrite(PIN_BIN1, LOW);
    digitalWrite(PIN_BIN2, LOW);
  } else if (forward) {
    digitalWrite(PIN_BIN1, HIGH);
    digitalWrite(PIN_BIN2, LOW);
  } else {
    digitalWrite(PIN_BIN1, LOW);
    digitalWrite(PIN_BIN2, HIGH);
  }
  ledcWrite(PWM_CHAN_B, speed);
}

void stopMotors() {
  setMotorLeft(0, true);
  setMotorRight(0, true);
}

void processCommand(float linear_x, float angular_z) {
  // Differential Drive Inverse Kinematics
  // v_left = v - (omega * L / 2)
  // v_right = v + (omega * L / 2)
  float v_left  = linear_x - (angular_z * WHEEL_BASE / 2.0);
  float v_right = linear_x + (angular_z * WHEEL_BASE / 2.0);

  // Map m/s (-0.6 to +0.6) to PWM (0 to 255)
  float max_speed = 0.6;
  int pwm_left  = (int)((abs(v_left) / max_speed) * 255.0);
  int pwm_right = (int)((abs(v_right) / max_speed) * 255.0);

  bool fwd_left  = (v_left >= 0);
  bool fwd_right = (v_right >= 0);

  setMotorLeft(pwm_left, fwd_left);
  setMotorRight(pwm_right, fwd_right);

  Serial.printf("Motors -> L: PWM %d (%s) | R: PWM %d (%s)\n",
                pwm_left, fwd_left ? "FWD" : "REV",
                pwm_right, fwd_right ? "FWD" : "REV");
}

void loop() {
  int packetSize = udp.parsePacket();
  if (packetSize) {
    char packetBuffer[64];
    int len = udp.read(packetBuffer, sizeof(packetBuffer) - 1);
    if (len > 0) {
      packetBuffer[len] = 0;
      String msg = String(packetBuffer);
      if (msg.startsWith("CMD,")) {
        int firstComma = msg.indexOf(',');
        int secondComma = msg.indexOf(',', firstComma + 1);
        if (firstComma > 0 && secondComma > 0) {
          float lin = msg.substring(firstComma + 1, secondComma).toFloat();
          float ang = msg.substring(secondComma + 1).toFloat();
          lastPacketTime = millis();
          processCommand(lin, ang);
        }
      }
    }
  }

  // Safety Timeout: If no command received in 500ms, emergency stop
  if (millis() - lastPacketTime > TIMEOUT_MS) {
    stopMotors();
  }
}
