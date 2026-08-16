/*
 * ESP32 Capacitive Touch & LED Dashboard Bridge for ROS 2 Gazebo Simulation
 * 
 * Hardware Setup:
 * - 4 Touch Wires (Jumper wires plugged into ESP32 with free ends to touch):
 *     GPIO 4  (Touch 0) -> Forward
 *     GPIO 15 (Touch 3) -> Backward
 *     GPIO 13 (Touch 4) -> Turn Left
 *     GPIO 27 (Touch 7) -> Turn Right
 * 
 * - 4 LEDs (connected via 220 Ohm resistors to GND):
 *     GPIO 18 -> Green LED (Forward)
 *     GPIO 19 -> Yellow LED 1 (Left Turn)
 *     GPIO 21 -> Yellow LED 2 (Right Turn)
 *     GPIO 22 -> Red LED (Stop / Brake)
 */

// Pin Definitions
const int TOUCH_FWD = 4;
const int TOUCH_BWD = 15;
const int TOUCH_LEFT = 13;
const int TOUCH_RIGHT = 27;

const int LED_GREEN = 18;
const int LED_YELLOW_L = 19;
const int LED_YELLOW_R = 21;
const int LED_RED = 22;

// Capacitive Touch Threshold (Untouched is ~70-90, touched drops below 40)
const int TOUCH_THRESHOLD = 40;

unsigned long lastSendTime = 0;
const unsigned long SEND_INTERVAL_MS = 50; // 20 Hz update rate

void setup() {
  Serial.begin(115200);
  
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW_L, OUTPUT);
  pinMode(LED_YELLOW_R, OUTPUT);
  pinMode(LED_RED, OUTPUT);

  // Initial State: Red ON (Stopped)
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_YELLOW_L, LOW);
  digitalWrite(LED_YELLOW_R, LOW);
  digitalWrite(LED_RED, HIGH);
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
  // 1. Check for incoming status/telemetry from ROS 2 Host
  // Format: "STATUS,<linear_x>,<angular_z>\n"
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

  // 2. Read Capacitive Touch inputs and send CMD to ROS 2
  if (millis() - lastSendTime >= SEND_INTERVAL_MS) {
    lastSendTime = millis();

    int valFwd = touchRead(TOUCH_FWD);
    int valBwd = touchRead(TOUCH_BWD);
    int valLeft = touchRead(TOUCH_LEFT);
    int valRight = touchRead(TOUCH_RIGHT);

    float linear_x = 0.0;
    float angular_z = 0.0;

    if (valFwd < TOUCH_THRESHOLD) {
      linear_x = 0.5;
    } else if (valBwd < TOUCH_THRESHOLD) {
      linear_x = -0.5;
    }

    if (valLeft < TOUCH_THRESHOLD) {
      angular_z = 1.0;
    } else if (valRight < TOUCH_THRESHOLD) {
      angular_z = -1.0;
    }

    // Send velocity command to PC: "CMD,<linear_x>,<angular_z>"
    Serial.print("CMD,");
    Serial.print(linear_x, 2);
    Serial.print(",");
    Serial.println(angular_z, 2);

    // If running standalone (without ROS node connected), update LEDs directly
    // based on touch state as immediate physical feedback
    updateLEDs(linear_x, angular_z);
  }
}
