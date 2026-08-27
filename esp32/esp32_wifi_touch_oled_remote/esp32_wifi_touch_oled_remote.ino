/*
 * Wireless ESP32 Handheld Remote with Continuous Adaptive Touch Tracking & OLED Dashboard
 * 
 * Auto-connects to 2.4 GHz Wi-Fi: "Brajesh"
 */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// 2.4 GHz WiFi Configuration
const char* ssid     = "Brajesh_2.4GHz";
const char* password = "Ash@0812#@";

// Laptop IP running ROS 2 (UDP Target)
const char* laptop_ip = "192.168.29.131";
const int   udp_port  = 8888;

WiFiUDP udp;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// OLED Custom I2C Pins (GPIO 32, GPIO 33)
const int PIN_OLED_SDA = 32;
const int PIN_OLED_SCL = 33;

// Touch Pins
const int PIN_TOUCH_FWD   = 4;
const int PIN_TOUCH_BWD   = 15;
const int PIN_TOUCH_LEFT  = 13;
const int PIN_TOUCH_RIGHT = 27;

// Breadboard LED Pins
const int LED_GREEN    = 18;
const int LED_YELLOW_L = 19;
const int LED_YELLOW_R = 21;
const int LED_RED      = 22;

// Continuous Adaptive Baselines
float baseFwd = 700.0, baseBwd = 700.0, baseLeft = 700.0, baseRight = 700.0;
const int TOUCH_DELTA = 100; // Drop of 100 below baseline triggers touch

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
  digitalWrite(LED_RED, HIGH);

  // OLED Init
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

  // Initial baseline sampling
  for (int i = 0; i < 15; i++) {
    baseFwd   = (baseFwd * 0.8)   + (touchRead(PIN_TOUCH_FWD) * 0.2);
    baseBwd   = (baseBwd * 0.8)   + (touchRead(PIN_TOUCH_BWD) * 0.2);
    baseLeft  = (baseLeft * 0.8)  + (touchRead(PIN_TOUCH_LEFT) * 0.2);
    baseRight = (baseRight * 0.8) + (touchRead(PIN_TOUCH_RIGHT) * 0.2);
    delay(20);
  }

  // Connect to 2.4 GHz Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to 2.4 GHz WiFi 'Brajesh'");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 25) {
    delay(400);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected! Remote IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi Connection Pending / Running Local.");
  }
}

void loop() {
  if (millis() - lastSendTime >= SEND_INTERVAL_MS) {
    lastSendTime = millis();

    int valFwd   = touchRead(PIN_TOUCH_FWD);
    int valBwd   = touchRead(PIN_TOUCH_BWD);
    int valLeft  = touchRead(PIN_TOUCH_LEFT);
    int valRight = touchRead(PIN_TOUCH_RIGHT);

    // Continuous Adaptive Touch Detection
    bool touchedFwd   = (valFwd < (baseFwd - TOUCH_DELTA));
    bool touchedBwd   = (valBwd < (baseBwd - TOUCH_DELTA));
    bool touchedLeft  = (valLeft < (baseLeft - TOUCH_DELTA));
    bool touchedRight = (valRight < (baseRight - TOUCH_DELTA));

    // Adapt baselines when untouched to smoothly handle battery voltage/temperature changes
    if (!touchedFwd)   baseFwd   = (baseFwd * 0.98)   + (valFwd * 0.02);
    if (!touchedBwd)   baseBwd   = (baseBwd * 0.98)   + (valBwd * 0.02);
    if (!touchedLeft)  baseLeft  = (baseLeft * 0.98)  + (valLeft * 0.02);
    if (!touchedRight) baseRight = (baseRight * 0.98) + (valRight * 0.02);

    float linear_x = 0.0;
    float angular_z = 0.0;

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

    // 3. Update LEDs and OLED
    updateLEDs(linear_x, angular_z);
    updateOLED(linear_x, angular_z, state_text, WiFi.status() == WL_CONNECTED);
  }
}
