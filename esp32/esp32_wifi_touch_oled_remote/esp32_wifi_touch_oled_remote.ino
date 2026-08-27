/*
 * Wireless ESP32 Handheld Remote with BOTH 4 LEDs + OLED Screen Active!
 * 
 * Hardware Wiring:
 * 1. 0.96" I2C OLED Display (SSD1306):
 *    - VDD (VCC) -> ESP32 3V3
 *    - GND       -> ESP32 GND
 *    - SDA       -> ESP32 GPIO 32 (D32)
 *    - SCK (SCL) -> ESP32 GPIO 33 (D33)
 * 
 * 2. 4 Touch Wires (Input Steering):
 *    - GPIO 4  (D4)  -> FORWARD
 *    - GPIO 15 (D15) -> BACKWARD
 *    - GPIO 13 (D13) -> TURN LEFT
 *    - GPIO 27 (D27) -> TURN RIGHT
 * 
 * 3. 4 Existing Breadboard LEDs (Kept in place!):
 *    - GPIO 18 (D18) -> Green LED (Forward)
 *    - GPIO 19 (D19) -> Yellow LED 1 (Left Turn)
 *    - GPIO 21 (D21) -> Yellow LED 2 (Right Turn)
 *    - GPIO 22 (D22) -> Red LED (Stop / Brake)
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// WiFi Configuration - Replace with your WiFi SSID and Password
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Laptop IP running ROS 2 (UDP Target)
const char* laptop_ip = "192.168.29.131";
const int   udp_port  = 8888;

WiFiUDP udp;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// OLED Custom I2C Pins (Leaving D21 and D22 free for your LEDs!)
const int PIN_OLED_SDA = 32;
const int PIN_OLED_SCL = 33;

// Touch Pins
const int PIN_TOUCH_FWD   = 4;
const int PIN_TOUCH_BWD   = 15;
const int PIN_TOUCH_LEFT  = 13;
const int PIN_TOUCH_RIGHT = 27;

// LED Pins
const int LED_GREEN    = 18;
const int LED_YELLOW_L = 19;
const int LED_YELLOW_R = 21;
const int LED_RED      = 22;

const int TOUCH_THRESHOLD = 350;

unsigned long lastSendTime = 0;
const unsigned long SEND_INTERVAL_MS = 50; // 20 Hz
bool oledAvailable = false;

void updateLEDs(float linear_x, float angular_z) {
  bool moving_fwd = (linear_x > 0.05);
  bool moving_bwd = (linear_x < -0.05);
  bool turning_left = (angular_z > 0.1);
  bool turning_right = (angular_z < -0.1);
  bool stopped = (!moving_fwd && !moving_bwd && !turning_left && !turning_right);

  digitalWrite(LED_GREEN, moving_fwd ? HIGH : LOW);
  digitalWrite(LED_RED, (stopped || moving_bwd) ? HIGH : LOW);
  digitalWrite(LED_YELLOW_L, turning_left ? HIGH : LOW);
  digitalWrite(LED_YELLOW_R, turning_right ? HIGH : LOW);
}

void updateOLED(float linear_x, float angular_z, const char* state_text, bool wifi_connected) {
  if (!oledAvailable) return;
  display.clearDisplay();

  // Header
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("AMR REMOTE");
  display.setCursor(75, 0);
  display.print(wifi_connected ? "[WiFi:OK]" : "[WiFi:--]");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  // State Text
  display.setCursor(0, 15);
  display.print("STATUS: ");
  display.print(state_text);

  // Velocity
  display.setCursor(0, 28);
  display.print("SPEED : ");
  display.print(linear_x >= 0 ? "+" : "");
  display.print(linear_x, 2);
  display.print(" m/s");

  display.setCursor(0, 40);
  display.print("STEER : ");
  display.print(angular_z >= 0 ? "+" : "");
  display.print(angular_z, 2);
  display.print(" rad/s");

  // Bottom action bar
  display.drawRect(0, 52, 128, 12, SSD1306_WHITE);
  display.setCursor(15, 54);
  if (linear_x > 0) display.print("^^ FORWARD ^^");
  else if (linear_x < 0) display.print("vv REVERSE vv");
  else if (angular_z > 0) display.print("<<< SPIN LEFT");
  else if (angular_z < 0) display.print("SPIN RIGHT >>>");
  else display.print("[   IDLE   ]");

  display.display();
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW_L, OUTPUT);
  pinMode(LED_YELLOW_R, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  digitalWrite(LED_RED, HIGH); // Red on at boot

  // OLED Init on D32 (SDA) and D33 (SCL)
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    oledAvailable = true;
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(10, 20);
    display.println("CONNECTING WIFI...");
    display.display();
  }

  // Connect to Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connection failed (offline mode).");
  }
}

void loop() {
  if (millis() - lastSendTime >= SEND_INTERVAL_MS) {
    lastSendTime = millis();

    int valFwd   = touchRead(PIN_TOUCH_FWD);
    int valBwd   = touchRead(PIN_TOUCH_BWD);
    int valLeft  = touchRead(PIN_TOUCH_LEFT);
    int valRight = touchRead(PIN_TOUCH_RIGHT);

    float linear_x = 0.0;
    float angular_z = 0.0;

    bool touchedFwd   = (valFwd < TOUCH_THRESHOLD);
    bool touchedBwd   = (valBwd < TOUCH_THRESHOLD);
    bool touchedLeft  = (valLeft < TOUCH_THRESHOLD);
    bool touchedRight = (valRight < TOUCH_THRESHOLD);

    const char* state_text = "STOPPED";

    if (touchedFwd && !touchedBwd) {
      linear_x = 0.5;
      state_text = "FORWARD";
    } else if (touchedBwd && !touchedFwd) {
      linear_x = -0.5;
      state_text = "REVERSE";
    }

    if (touchedLeft && !touchedRight) {
      angular_z = 1.0;
      if (touchedFwd) state_text = "FWD+LEFT";
      else if (touchedBwd) state_text = "REV+LEFT";
      else state_text = "SPIN LEFT";
    } else if (touchedRight && !touchedLeft) {
      angular_z = -1.0;
      if (touchedFwd) state_text = "FWD+RIGHT";
      else if (touchedBwd) state_text = "REV+RIGHT";
      else state_text = "SPIN RIGHT";
    }

    // 1. Send UDP packet over Wi-Fi
    if (WiFi.status() == WL_CONNECTED) {
      char msg[64];
      snprintf(msg, sizeof(msg), "CMD,%.2f,%.2f", linear_x, angular_z);
      udp.beginPacket(laptop_ip, udp_port);
      udp.write((const uint8_t*)msg, strlen(msg));
      udp.endPacket();
    }

    // 2. Update Both LEDs and OLED Screen simultaneously
    updateLEDs(linear_x, angular_z);
    updateOLED(linear_x, angular_z, state_text, WiFi.status() == WL_CONNECTED);
  }
}
