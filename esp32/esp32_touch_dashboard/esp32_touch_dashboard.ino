/*
 * ESP32 Capacitive Touch & LED Dashboard Bridge for ROS 2 Gazebo Simulation
 * 
 * Hardware Setup:
 * - 4 Touch Wires (Jumper wires plugged into ESP32 with free ends to touch):
 *     GPIO 4  (Touch 0 / D4)  -> Forward
 *     GPIO 15 (Touch 3 / D15) -> Backward
 *     GPIO 13 (Touch 4 / D13) -> Turn Left
 *     GPIO 27 (Touch 7 / D27) -> Turn Right
 * 
 * - 4 LEDs (connected via 220 Ohm resistors to GND):
 *     GPIO 18 -> Green LED (Forward)
 *     GPIO 19 -> Yellow LED 1 (Left Turn)
 *     GPIO 21 -> Yellow LED 2 (Right Turn)
 *     GPIO 22 -> Red LED (Stop / Brake)
 */

// Pin Definitions
const int PIN_TOUCH_FWD   = 4;   // Touch channel T0
const int PIN_TOUCH_BWD   = 15;  // Touch channel T3
const int PIN_TOUCH_LEFT  = 13;  // Touch channel T4
const int PIN_TOUCH_RIGHT = 27;  // Touch channel T7

const int LED_GREEN    = 18;
const int LED_YELLOW_L = 19;
const int LED_YELLOW_R = 21;
const int LED_RED      = 22;

// Touch threshold: Untouched is ~800-1000, Touched is ~40-210.
// A threshold of 350 gives clean, instant, noise-free triggering!
const int TOUCH_THRESHOLD = 350;

unsigned long lastSendTime = 0;
const unsigned long SEND_INTERVAL_MS = 50; // 20 Hz update rate

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW_L, OUTPUT);
  pinMode(LED_YELLOW_R, OUTPUT);
  pinMode(LED_RED, OUTPUT);

  // Initial State: Red ON (Stopped)
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_YELLOW_L, LOW);
  digitalWrite(LED_YELLOW_R, LOW);
  digitalWrite(LED_RED, HIGH);

  Serial.println("\n--- ESP32 Touch Controller Ready (Threshold: 350) ---");
}

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

void loop() {
  // 1. Check for incoming telemetry from ROS 2 Node (overrides local LED state if ROS active)
  if (Serial.available() > 0) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.startsWith("STATUS,")) {
      int firstComma = line.indexOf(',');
      int secondComma = line.indexOf(',', firstComma + 1);
      if (firstComma > 0 && secondComma > 0) {
        float lin = line.substring(firstComma + 1, secondComma).toFloat();
        float ang = line.substring(secondComma + 1).toFloat();
        updateLEDs(lin, ang);
      }
    }
  }

  // 2. Read Touch Sensors at 20 Hz
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

    // Linear speed calculation
    if (touchedFwd && !touchedBwd) {
      linear_x = 0.5;
    } else if (touchedBwd && !touchedFwd) {
      linear_x = -0.5;
    }

    // Steering calculation (Supports simultaneous touches like Forward + Turn!)
    if (touchedLeft && !touchedRight) {
      angular_z = 1.0;
    } else if (touchedRight && !touchedLeft) {
      angular_z = -1.0;
    }

    // Print command for ROS 2 Serial Bridge
    Serial.print("CMD,");
    Serial.print(linear_x, 2);
    Serial.print(",");
    Serial.println(angular_z, 2);

    // Update physical breadboard LEDs immediately for standalone visual feedback
    updateLEDs(linear_x, angular_z);
  }
}
