#include <Arduino.h>

// ESP32 GPIO Signal Pins: 0=Base(13), 1=Shoulder(26), 2=Elbow(14), 3=Gripper(27)
const int SERVO_PINS[4] = {13, 26, 14, 27};
const char* JOINT_NAMES[4] = {"Base", "Shldr", "Elbow", "Grip"};

// Current pulse widths in microseconds (1500us = 90-degree center)
float currentUs[4] = {1500.0, 1500.0, 1500.0, 1500.0};

// Safe limits so SG90s never hit internal plastic stops (750us to 2150us)
const float MIN_US    = 750.0;
const float MAX_US    = 2150.0;
const float CENTER_US = 1500.0;

// Gentle movement speed (microseconds per 20ms frame) to keep current draw low
const float MAX_US_STEP = 8.0;

// Track when each motor last moved so we can mute idle buzzing
unsigned long lastMoveTime[4] = {0, 0, 0, 0};
bool isMuted[4]               = {false, false, false, false};
const unsigned long MUTE_DELAY = 250; // Silence motor 250ms after it stops moving

// Live joystick states from Python
int joyLX = 0, joyLY = 0, joyRX = 0, joyRY = 0, joyBtnX = 0;
unsigned long lastPacketTime = 0;
unsigned long lastServoTick  = 0;
String serialBuffer = "";

// Convert microseconds (750-2150us) to 14-bit 50Hz hardware PWM duty cycle
uint32_t usToDuty(int us) {
  return (uint32_t)((us * 16384UL) / 20000UL);
}

// Write pulse to hardware PWM (or 0 to completely silence the motor coils)
void writeHardwarePwm(int index, uint32_t duty) {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcWrite(SERVO_PINS[index], duty);
#else
  ledcWrite(index, duty);
#endif
}

void setupServoPin(int index) {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcAttach(SERVO_PINS[index], 50, 14);
#else
  ledcSetup(index, 50, 14);
  ledcAttachPin(SERVO_PINS[index], index);
#endif
  // Center one motor at a time on startup, wait 300ms, then mute it
  writeHardwarePwm(index, usToDuty((int)CENTER_US));
  delay(300);
  writeHardwarePwm(index, 0);
  isMuted[index] = true;
}

void parsePacket(String line) {
  int lx = 0, ly = 0, rx = 0, ry = 0, btnX = 0;
  if (sscanf(line.c_str(), "%d,%d,%d,%d,%d", &lx, &ly, &rx, &ry, &btnX) == 5) {
    if (abs(lx) <= 255 && abs(ly) <= 255 && abs(rx) <= 255 && abs(ry) <= 255) {
      joyLX = lx;
      joyLY = ly;
      joyRX = rx;
      joyRY = ry;
      joyBtnX = btnX;
      lastPacketTime = millis();
    }
  }
}

void setup() {
  Serial.begin(115200);
  serialBuffer.reserve(64);

  for (int i = 0; i < 4; i++) {
    setupServoPin(i);
  }
}

void loop() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n') {
      serialBuffer.trim();
      if (serialBuffer.length() > 0) {
        parsePacket(serialBuffer);
      }
      serialBuffer = "";
    } else if (serialBuffer.length() < 60) {
      serialBuffer += c;
    }
  }

  if (millis() - lastPacketTime > 200) {
    joyLX = 0; joyLY = 0; joyRX = 0; joyRY = 0; joyBtnX = 0;
  }

  if (millis() - lastServoTick >= 20) {
    unsigned long now = millis();
    lastServoTick = now;

    // 0=Base(LX), 1=Shoulder(-LY), 2=Elbow(-RY), 3=Gripper(RX)
    int inputs[4] = { joyLX, -joyLY, -joyRY, joyRX };

    int activeMotor = -1;
    int maxDeflection = 35;

    for (int i = 0; i < 4; i++) {
      if (abs(inputs[i]) > maxDeflection) {
        maxDeflection = abs(inputs[i]);
        activeMotor = i;
      }
    }

    // Sequential single-motor center reset when holding 'X'
    if (joyBtnX == 1) {
      for (int i = 0; i < 4; i++) {
        if (abs(currentUs[i] - CENTER_US) > 10.0) {
          activeMotor = i;
          inputs[i] = (CENTER_US > currentUs[i]) ? 180 : -180;
          break;
        }
      }
    }

    for (int i = 0; i < 4; i++) {
      if (i == activeMotor) {
        for (int j = 0; j < 4; j++) {
          if (j != activeMotor && !isMuted[j]) {
            writeHardwarePwm(j, 0);
            isMuted[j] = true;
          }
        }

        currentUs[i] += (inputs[i] / 255.0) * MAX_US_STEP;
        currentUs[i] = constrain(currentUs[i], MIN_US, MAX_US);

        writeHardwarePwm(i, usToDuty((int)round(currentUs[i])));
        lastMoveTime[i] = now;
        isMuted[i] = false;

        int deg = map((int)round(currentUs[i]), (int)MIN_US, (int)MAX_US, 15, 165);
        Serial.printf("[MOVING ONLY %s] Angle: %3d deg (%4dus)\n",
                      JOINT_NAMES[i], deg, (int)round(currentUs[i]));
      } else {
        if (!isMuted[i] && (now - lastMoveTime[i] > MUTE_DELAY)) {
          writeHardwarePwm(i, 0);
          isMuted[i] = true;
        }
      }
    }
  }
}
