/*
 * Wireless ESP32 Handheld Capacitive Touch Remote with OLED Dashboard
 * 
 * Hardware Wiring:
 * 1. 0.96" I2C OLED Display:
 *    - VCC -> ESP32 3V3
 *    - GND -> ESP32 GND
 *    - SCL -> GPIO 22 (D22)
 *    - SDA -> GPIO 21 (D21)
 * 
 * 2. 4 Touch Wires:
 *    - GPIO 4  (D4)  -> FORWARD
 *    - GPIO 15 (D15) -> BACKWARD
 *    - GPIO 13 (D13) -> TURN LEFT
 *    - GPIO 27 (D27) -> TURN RIGHT
 * 
 * 3. Power:
 *    - 3.5V 18650 Battery (+) -> ESP32 VIN (or 3V3)
 *    - 3.5V 18650 Battery (-) -> ESP32 GND
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

// Touch Pins
const int PIN_TOUCH_FWD   = 4;
const int PIN_TOUCH_BWD   = 15;
const int PIN_TOUCH_LEFT  = 13;
const int PIN_TOUCH_RIGHT = 27;

const int TOUCH_THRESHOLD = 350;

unsigned long lastSendTime = 0;
const unsigned long SEND_INTERVAL_MS = 50; // 20 Hz
bool oledAvailable = false;

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

  // OLED Init
  Wire.begin(21, 22);
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
    Serial.println("\nWiFi connection failed! Running in offline/debug mode.");
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

    // 2. Also print to Serial for USB debugging
    Serial.printf("CMD,%.2f,%.2f\n", linear_x, angular_z);

    // 3. Update OLED Screen
    updateOLED(linear_x, angular_z, state_text, WiFi.status() == WL_CONNECTED);
  }
}
