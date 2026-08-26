/*
 * ESP32 Handheld Capacitive Touch Remote with 0.96" I2C OLED Dashboard
 * 
 * Hardware Wiring:
 * 1. 0.96" I2C OLED Display (SSD1306):
 *    - VCC -> ESP32 3V3
 *    - GND -> ESP32 GND
 *    - SCL -> ESP32 GPIO 22 (D22)
 *    - SDA -> ESP32 GPIO 21 (D21)
 * 
 * 2. 4 Touch Wires:
 *    - GPIO 4  (Touch 0 / D4)  -> FORWARD
 *    - GPIO 15 (Touch 3 / D15) -> BACKWARD
 *    - GPIO 13 (Touch 4 / D13) -> TURN LEFT
 *    - GPIO 27 (Touch 7 / D27) -> TURN RIGHT
 * 
 * Required Libraries in Arduino IDE:
 * - Adafruit SSD1306
 * - Adafruit GFX Library
 * (Or install via Library Manager: "Adafruit SSD1306")
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Touch Pins
const int PIN_TOUCH_FWD   = 4;
const int PIN_TOUCH_BWD   = 15;
const int PIN_TOUCH_LEFT  = 13;
const int PIN_TOUCH_RIGHT = 27;

// Calibrated Touch Threshold
const int TOUCH_THRESHOLD = 350;

unsigned long lastSendTime = 0;
const unsigned long SEND_INTERVAL_MS = 50; // 20 Hz update rate
bool oledAvailable = false;

void setup() {
  Serial.begin(115200);
  delay(300);

  // Initialize I2C OLED (SDA=21, SCL=22)
  Wire.begin(21, 22);
  if (display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    oledAvailable = true;
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(15, 20);
    display.println("AMR TOUCH REMOTE");
    display.setCursor(25, 35);
    display.println("INITIALIZING...");
    display.display();
    delay(1000);
  } else {
    Serial.println("Warning: OLED not detected at 0x3C (checking 0x3D or running in headless mode)");
  }

  Serial.println("\n--- ESP32 Touch Remote with OLED Ready ---");
}

void drawDashboard(float linear_x, float angular_z, const char* state_text) {
  if (!oledAvailable) return;

  display.clearDisplay();

  // Top Header Banner
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("AMR REMOTE");
  display.setCursor(85, 0);
  display.print("[LIVE]");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  // Mode / Direction Text
  display.setTextSize(1);
  display.setCursor(0, 16);
  display.print("ACTION:");
  display.setTextSize(1);
  display.setCursor(48, 16);
  display.print(state_text);

  // Linear Velocity Bar / Text
  display.setCursor(0, 30);
  display.print("SPEED: ");
  display.print(linear_x >= 0 ? "+" : "");
  display.print(linear_x, 2);
  display.print(" m/s");

  // Angular Yaw Rate
  display.setCursor(0, 42);
  display.print("STEER: ");
  display.print(angular_z >= 0 ? "+" : "");
  display.print(angular_z, 2);
  display.print(" rad/s");

  // Visual Direction Indicators Box at bottom
  display.drawRect(0, 53, 128, 11, SSD1306_WHITE);
  if (linear_x > 0) {
    display.setCursor(55, 55);
    display.print("^ FWD ^");
  } else if (linear_x < 0) {
    display.setCursor(55, 55);
    display.print("v REV v");
  } else if (angular_z > 0) {
    display.setCursor(40, 55);
    display.print("<<< LEFT");
  } else if (angular_z < 0) {
    display.setCursor(65, 55);
    display.print("RIGHT >>>");
  } else {
    display.setCursor(48, 55);
    display.print("[ IDLE ]");
  }

  display.display();
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

    // Linear speed
    if (touchedFwd && !touchedBwd) {
      linear_x = 0.5;
      state_text = "FORWARD";
    } else if (touchedBwd && !touchedFwd) {
      linear_x = -0.5;
      state_text = "REVERSE";
    }

    // Steering speed
    if (touchedLeft && !touchedRight) {
      angular_z = 1.0;
      if (touchedFwd) state_text = "FWD + LEFT";
      else if (touchedBwd) state_text = "REV + LEFT";
      else state_text = "SPIN LEFT";
    } else if (touchedRight && !touchedLeft) {
      angular_z = -1.0;
      if (touchedFwd) state_text = "FWD + RIGHT";
      else if (touchedBwd) state_text = "REV + RIGHT";
      else state_text = "SPIN RIGHT";
    }

    // Send command to ROS 2 over Serial
    Serial.print("CMD,");
    Serial.print(linear_x, 2);
    Serial.print(",");
    Serial.println(angular_z, 2);

    // Update OLED graphics
    drawDashboard(linear_x, angular_z, state_text);
  }
}
