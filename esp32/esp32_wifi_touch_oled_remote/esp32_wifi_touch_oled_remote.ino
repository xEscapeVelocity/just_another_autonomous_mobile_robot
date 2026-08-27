/*
 * Wireless ESP32 Handheld Remote with Rock-Solid Directional Touch Logic & OLED Dashboard
 * 
 * Fix: Baseline is strictly prevented from adapting downwards while touched,
 * ensuring continuous hold works across all 4 channels (Forward, Backward, Left, Right).
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

// OLED Custom I2C Pins
const int PIN_OLED_SDA = 32;
const int PIN_OLED_SCL = 33;

// Touch Pins
const int PIN_TOUCH_FWD   = 4;   // Touch 0
const int PIN_TOUCH_BWD   = 15;  // Touch 3
const int PIN_TOUCH_LEFT  = 13;  // Touch 4
const int PIN_TOUCH_RIGHT = 27;  // Touch 7

// LED Pins
const int LED_GREEN    = 18;
const int LED_YELLOW_L = 19;
const int LED_YELLOW_R = 21;
const int LED_RED      = 22;

// Untouched Reference Baselines
int baseFwd = 700, baseBwd = 700, baseLeft = 700, baseRight = 700;

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

void updateOLED(float linear_x, float angular_z, const char* state_text, bool wifi_connected, int vF, int vB, int vL, int vR) {
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
  display.setCursor(0, 14);
  display.print("ACTION: ");
  display.print(state_text);

  // Velocity
  display.setCursor(0, 26);
  display.print("SPEED : ");
  display.print(linear_x >= 0 ? "+" : "");
  display.print(linear_x, 2);
  display.print(" m/s");

  display.setCursor(0, 37);
  display.print("STEER : ");
  display.print(angular_z >= 0 ? "+" : "");
  display.print(angular_z, 2);
  display.print(" rad/s");

  // Bottom action bar
  display.drawRect(0, 48, 128, 16, SSD1306_WHITE);
  display.setCursor(15, 52);
  if (linear_x > 0 && angular_z > 0)       display.print("^ FWD + LEFT ^");
  else if (linear_x > 0 && angular_z < 0)  display.print("^ FWD + RIGHT ^");
  else if (linear_x < 0 && angular_z > 0)  display.print("v REV + LEFT v");
  else if (linear_x < 0 && angular_z < 0)  display.print("v REV + RIGHT v");
  else if (linear_x > 0)                   display.print("^^ FORWARD ^^");
  else if (linear_x < 0)                   display.print("vv REVERSE vv");
  else if (angular_z > 0)                  display.print("<<< SPIN LEFT");
  else if (angular_z < 0)                  display.print("SPIN RIGHT >>>");
  else                                     display.print("[   IDLE   ]");

  display.display();
}

void calibrateBaselines() {
  long sumF = 0, sumB = 0, sumL = 0, sumR = 0;
  int samples = 25;
  for (int i = 0; i < samples; i++) {
    sumF += touchRead(PIN_TOUCH_FWD);
    sumB += touchRead(PIN_TOUCH_BWD);
    sumL += touchRead(PIN_TOUCH_LEFT);
    sumR += touchRead(PIN_TOUCH_RIGHT);
    delay(20);
  }
  baseFwd   = max(sumF / samples, 500L);
  baseBwd   = max(sumB / samples, 500L);
  baseLeft  = max(sumL / samples, 500L);
  baseRight = max(sumR / samples, 500L);
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
    display.println("CALIBRATING...");
    display.display();
  }

  // Sample pristine untouched baselines
  calibrateBaselines();

  if (oledAvailable) {
    display.clearDisplay();
    display.setCursor(10, 20);
    display.println("CONNECTING WIFI...");
    display.display();
  }

  // Connect to 2.4 GHz Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to 2.4 GHz WiFi 'Brajesh_2.4GHz'");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 25) {
    delay(350);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected! Remote IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi Offline / Continuing local.");
  }
}

void loop() {
  if (millis() - lastSendTime >= SEND_INTERVAL_MS) {
    lastSendTime = millis();

    int valFwd   = touchRead(PIN_TOUCH_FWD);
    int valBwd   = touchRead(PIN_TOUCH_BWD);
    int valLeft  = touchRead(PIN_TOUCH_LEFT);
    int valRight = touchRead(PIN_TOUCH_RIGHT);

    // Dynamic thresholds: Trigger if reading drops below 75% of untouched baseline OR below absolute 450
    bool touchedFwd   = (valFwd   < (int)(baseFwd   * 0.78)) || (valFwd   < 420);
    bool touchedBwd   = (valBwd   < (int)(baseBwd   * 0.78)) || (valBwd   < 420);
    bool touchedLeft  = (valLeft  < (int)(baseLeft  * 0.78)) || (valLeft  < 420);
    bool touchedRight = (valRight < (int)(baseRight * 0.78)) || (valRight < 420);

    // Only allow baselines to slowly track UPWARDS (never downwards while touching!)
    if (valFwd   > baseFwd)   baseFwd   = (int)((baseFwd   * 0.95) + (valFwd   * 0.05));
    if (valBwd   > baseBwd)   baseBwd   = (int)((baseBwd   * 0.95) + (valBwd   * 0.05));
    if (valLeft  > baseLeft)  baseLeft  = (int)((baseLeft  * 0.95) + (valLeft  * 0.05));
    if (valRight > baseRight) baseRight = (int)((baseRight * 0.95) + (valRight * 0.05));

    float linear_x = 0.0;
    float angular_z = 0.0;

    const char* state_text = "STOPPED";

    // Forward / Reverse
    if (touchedFwd && !touchedBwd) {
      linear_x = 0.5;
      state_text = "FORWARD";
    } else if (touchedBwd && !touchedFwd) {
      linear_x = -0.5;
      state_text = "REVERSE";
    }

    // Steering (Left / Right) + Combined Diagonal Touch
    if (touchedLeft && !touchedRight) {
      angular_z = 1.0;
      if (touchedFwd)       state_text = "FWD + LEFT";
      else if (touchedBwd)  state_text = "REV + LEFT";
      else                  state_text = "SPIN LEFT";
    } else if (touchedRight && !touchedLeft) {
      angular_z = -1.0;
      if (touchedFwd)       state_text = "FWD + RIGHT";
      else if (touchedBwd)  state_text = "REV + RIGHT";
      else                  state_text = "SPIN RIGHT";
    }

    // 1. Send UDP packet over Wi-Fi
    if (WiFi.status() == WL_CONNECTED) {
      char msg[64];
      snprintf(msg, sizeof(msg), "CMD,%.2f,%.2f", linear_x, angular_z);
      udp.beginPacket(laptop_ip, udp_port);
      udp.write((const uint8_t*)msg, strlen(msg));
      udp.endPacket();
    }

    // 2. Debug print
    Serial.printf("RAW[F:%d B:%d L:%d R:%d] -> CMD,%.2f,%.2f\n", valFwd, valBwd, valLeft, valRight, linear_x, angular_z);

    // 3. Update LEDs and OLED
    updateLEDs(linear_x, angular_z);
    updateOLED(linear_x, angular_z, state_text, WiFi.status() == WL_CONNECTED, valFwd, valBwd, valLeft, valRight);
  }
}
