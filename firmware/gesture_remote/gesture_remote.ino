#include <Wire.h>
#include <U8g2lib.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <ir_Samsung.h>
#include <math.h>

#include "gesture_model.h"

// ================== HARDWARE ==================
constexpr uint8_t IR_LED_PIN = 18;
constexpr uint8_t BTN_UP = 33;
constexpr uint8_t BTN_DOWN = 25;
constexpr uint8_t BTN_SELECT = 32;
constexpr uint8_t SDA_PIN = 21;
constexpr uint8_t SCL_PIN = 22;
constexpr uint8_t MPU6050_ADDRESS = 0x68;

IRsend irsend(IR_LED_PIN);
IRSamsungAc ac(IR_LED_PIN);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ================== GESTURE MODEL ==================
// Set to 1 only after installing a compatible KNN model. Decision Tree is smaller by default.
#define USE_KNN_MODEL 0

constexpr uint8_t WINDOW_SAMPLES = 20;
constexpr uint16_t SAMPLE_INTERVAL_MS = 50;
constexpr float MOTION_RANGE_THRESHOLD_G = 0.25f;
constexpr uint16_t GESTURE_COOLDOWN_MS = 900;

enum GestureId : int8_t {
  GESTURE_UP = 0,
  GESTURE_DOWN = 1,
  GESTURE_LEFT = 2,
  GESTURE_RIGHT = 3,
  GESTURE_UNKNOWN = -1
};

float axWindow[WINDOW_SAMPLES];
float ayWindow[WINDOW_SAMPLES];
float azWindow[WINDOW_SAMPLES];
uint8_t sampleCount = 0;
uint32_t lastSampleTime = 0;
uint32_t lastGestureTime = 0;
bool mpuReady = false;

// ================== MENU ==================
enum Page { MAIN_MENU, SUB_MENU };
Page currentPage = MAIN_MENU;

const char *devices[] = {"SONY TV", "DTH", "SAMSUNG AC"};
const char *sonySignals[] = {
  "POWER", "VOL +", "VOL -", "CH +", "CH -",
  "MENU", "UP", "DOWN", "LEFT", "RIGHT"
};
const char *dthSignals[] = {"POWER", "VOLUME", "CHANNEL"};
const char *acSignals[] = {"ON", "OFF", "TEMP +", "TEMP -"};

int deviceIndex = 0;
int signalIndex = 0;
bool acPower = false;
uint8_t acTemp = 24;
uint32_t lastActionTime = 0;
uint32_t lastButtonTime = 0;

constexpr uint32_t RETURN_TIME_MS = 20000;
constexpr uint16_t BUTTON_DEBOUNCE_MS = 250;

// ================== MPU6050 ==================
void writeMpuRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MPU6050_ADDRESS);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission(true);
}

bool initializeMpu6050() {
  writeMpuRegister(0x6B, 0x00);
  delay(100);
  writeMpuRegister(0x1C, 0x00);  // +/-2 g.
  writeMpuRegister(0x1A, 0x03);  // Low-pass filter.

  Wire.beginTransmission(MPU6050_ADDRESS);
  Wire.write(0x75);
  if (Wire.endTransmission(false) != 0) return false;
  Wire.requestFrom(MPU6050_ADDRESS, static_cast<uint8_t>(1), true);
  return Wire.available() && Wire.read() == 0x68;
}

bool readAcceleration(float &ax, float &ay, float &az) {
  Wire.beginTransmission(MPU6050_ADDRESS);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  Wire.requestFrom(MPU6050_ADDRESS, static_cast<uint8_t>(6), true);
  if (Wire.available() != 6) return false;

  const int16_t rawX = (Wire.read() << 8) | Wire.read();
  const int16_t rawY = (Wire.read() << 8) | Wire.read();
  const int16_t rawZ = (Wire.read() << 8) | Wire.read();
  constexpr float COUNTS_PER_G = 16384.0f;
  ax = rawX / COUNTS_PER_G;
  ay = rawY / COUNTS_PER_G;
  az = rawZ / COUNTS_PER_G;
  return true;
}

void axisFeatures(const float *values, float *features, uint8_t meanIndex,
                  uint8_t stdIndex, uint8_t minIndex, uint8_t maxIndex,
                  uint8_t deltaIndex) {
  float sum = 0.0f;
  float minimum = values[0];
  float maximum = values[0];

  for (uint8_t i = 0; i < WINDOW_SAMPLES; ++i) {
    sum += values[i];
    if (values[i] < minimum) minimum = values[i];
    if (values[i] > maximum) maximum = values[i];
  }

  const float mean = sum / WINDOW_SAMPLES;
  float variance = 0.0f;
  for (uint8_t i = 0; i < WINDOW_SAMPLES; ++i) {
    const float difference = values[i] - mean;
    variance += difference * difference;
  }

  features[meanIndex] = mean;
  features[stdIndex] = sqrtf(variance / WINDOW_SAMPLES);
  features[minIndex] = minimum;
  features[maxIndex] = maximum;
  features[deltaIndex] = values[WINDOW_SAMPLES - 1] - values[0];
}

void extractFeatures(float *features) {
  // Exact feature order expected by the embedded model:
  // mean xyz, std xyz, min xyz, max xyz, delta xyz.
  axisFeatures(axWindow, features, 0, 3, 6, 9, 12);
  axisFeatures(ayWindow, features, 1, 4, 7, 10, 13);
  axisFeatures(azWindow, features, 2, 5, 8, 11, 14);
}

bool containsMotion(const float *features) {
  const float xRange = features[9] - features[6];
  const float yRange = features[10] - features[7];
  const float zRange = features[11] - features[8];
  float largestRange = xRange;
  if (yRange > largestRange) largestRange = yRange;
  if (zRange > largestRange) largestRange = zRange;
  return largestRange >= MOTION_RANGE_THRESHOLD_G;
}

int predictTiltFallback(const float *features) {
  // Provides basic navigation before a trained header is exported.
  // The trained model should be used for the final hardware demonstration.
  float xMotion = features[12];
  float yMotion = features[13];

  if (fabsf(xMotion) < 0.10f && fabsf(yMotion) < 0.10f) {
    xMotion = features[0];
    yMotion = features[1];
  }

  if (fabsf(xMotion) >= fabsf(yMotion)) {
    return xMotion >= 0.0f ? GESTURE_RIGHT : GESTURE_LEFT;
  }
  return yMotion >= 0.0f ? GESTURE_UP : GESTURE_DOWN;
}

const char *gestureName(int gesture) {
  switch (gesture) {
    case GESTURE_UP: return "UP";
    case GESTURE_DOWN: return "DOWN";
    case GESTURE_LEFT: return "LEFT";
    case GESTURE_RIGHT: return "RIGHT";
    default: return "UNKNOWN";
  }
}

// ================== DISPLAY ==================
int commandCount() {
  if (deviceIndex == 0) return 10;
  if (deviceIndex == 1) return 3;
  return 4;
}

const char *commandName(int index) {
  if (deviceIndex == 0) return sonySignals[index];
  if (deviceIndex == 1) return dthSignals[index];
  return acSignals[index];
}

void drawMainMenu() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 11, "Select Device");
  for (int i = 0; i < 3; ++i) {
    if (i == deviceIndex) u8g2.drawStr(0, 29 + i * 11, ">");
    u8g2.drawStr(12, 29 + i * 11, devices[i]);
  }
  u8g2.sendBuffer();
}

void drawSubMenu() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 11, devices[deviceIndex]);

  const int count = commandCount();
  int firstVisible = signalIndex - 1;
  if (firstVisible < 0) firstVisible = 0;
  if (firstVisible > count - 4) firstVisible = count - 4;
  if (firstVisible < 0) firstVisible = 0;

  for (int row = 0; row < 4 && firstVisible + row < count; ++row) {
    const int index = firstVisible + row;
    if (index == signalIndex) u8g2.drawStr(0, 27 + row * 10, ">");
    u8g2.drawStr(12, 27 + row * 10, commandName(index));
  }
  u8g2.sendBuffer();
}

void drawCurrentPage() {
  currentPage == MAIN_MENU ? drawMainMenu() : drawSubMenu();
}

void showFeedback(const char *title, const char *message) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 15, title);
  u8g2.drawStr(0, 38, message);
  u8g2.sendBuffer();
}

// ================== INFRARED ==================
void sendSony(uint16_t code) {
  for (uint8_t i = 0; i < 3; ++i) {
    irsend.sendSony(code, 12);
    delay(40);
  }
}

void sendDth(uint64_t code, uint8_t bits) {
  irsend.sendRC6(code, bits);
}

void sendSelectedSignal() {
  if (deviceIndex == 0) {
    // These values are preserved from the supplied prototype code.
    // Re-capture them if a different Sony model responds differently.
    const uint16_t codes[] = {
      0xA90, 0xA50, 0x2F0, 0x490, 0xC90,
      0xA50, 0xAF0, 0x2D0, 0xCD0, 0x2F0
    };
    sendSony(codes[signalIndex]);
  } else if (deviceIndex == 1) {
    const uint64_t codes[] = {0xC8041A70CULL, 0xC80412711ULL, 0x1900834EULL};
    const uint8_t bits[] = {36, 36, 29};
    sendDth(codes[signalIndex], bits[signalIndex]);
  } else {
    if (signalIndex == 0) acPower = true;
    if (signalIndex == 1) acPower = false;
    if (signalIndex == 2 && acTemp < 30) ++acTemp;
    if (signalIndex == 3 && acTemp > 18) --acTemp;

    if (acPower) {
      ac.on();
      ac.setMode(kSamsungAcCool);
      ac.setTemp(acTemp);
      ac.setFan(kSamsungAcFanAuto);
    } else {
      ac.off();
    }
    ac.send();
  }

  Serial.print("Sent: ");
  Serial.print(devices[deviceIndex]);
  Serial.print(" / ");
  Serial.println(commandName(signalIndex));
  showFeedback("IR command sent", commandName(signalIndex));
  delay(450);
}

// ================== NAVIGATION ==================
void moveSelection(int direction) {
  if (currentPage == MAIN_MENU) {
    deviceIndex += direction;
    if (deviceIndex < 0) deviceIndex = 2;
    if (deviceIndex > 2) deviceIndex = 0;
  } else {
    signalIndex += direction;
    if (signalIndex < 0) signalIndex = commandCount() - 1;
    if (signalIndex >= commandCount()) signalIndex = 0;
  }
  drawCurrentPage();
}

void selectCurrentItem() {
  if (currentPage == MAIN_MENU) {
    currentPage = SUB_MENU;
    signalIndex = 0;
  } else {
    sendSelectedSignal();
  }
  drawCurrentPage();
}

void returnToMainMenu() {
  currentPage = MAIN_MENU;
  signalIndex = 0;
  drawMainMenu();
}

void applyGesture(int gesture) {
  Serial.print("Gesture: ");
  Serial.println(gestureName(gesture));
  showFeedback("Gesture detected", gestureName(gesture));
  delay(300);

  switch (gesture) {
    case GESTURE_UP: moveSelection(-1); break;
    case GESTURE_DOWN: moveSelection(1); break;
    case GESTURE_LEFT: returnToMainMenu(); break;
    case GESTURE_RIGHT: selectCurrentItem(); break;
    default: drawCurrentPage(); break;
  }
  lastActionTime = millis();
}

void processGestureWindow() {
  float features[GESTURE_FEATURE_COUNT];
  extractFeatures(features);
  if (!containsMotion(features)) return;
  if (millis() - lastGestureTime < GESTURE_COOLDOWN_MS) return;

  int prediction;
#if GESTURE_MODEL_GENERATED
  prediction = USE_KNN_MODEL ? predictKnn(features) : predictDecisionTree(features);
#else
  prediction = predictTiltFallback(features);
#endif

  if (prediction >= GESTURE_UP && prediction <= GESTURE_RIGHT) {
    lastGestureTime = millis();
    applyGesture(prediction);
  }
}

void updateGestureSampling() {
  if (!mpuReady || millis() - lastSampleTime < SAMPLE_INTERVAL_MS) return;
  lastSampleTime = millis();

  if (!readAcceleration(axWindow[sampleCount], ayWindow[sampleCount],
                        azWindow[sampleCount])) {
    Serial.println("MPU6050 read failed");
    return;
  }

  ++sampleCount;
  if (sampleCount == WINDOW_SAMPLES) {
    processGestureWindow();
    sampleCount = 0;
  }
}

void updateButtons() {
  if (millis() - lastButtonTime < BUTTON_DEBOUNCE_MS) return;

  if (digitalRead(BTN_UP) == LOW) {
    lastButtonTime = millis();
    lastActionTime = millis();
    moveSelection(-1);
  } else if (digitalRead(BTN_DOWN) == LOW) {
    lastButtonTime = millis();
    lastActionTime = millis();
    moveSelection(1);
  } else if (digitalRead(BTN_SELECT) == LOW) {
    lastButtonTime = millis();
    lastActionTime = millis();
    selectCurrentItem();
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);

  Wire.begin(SDA_PIN, SCL_PIN);
  u8g2.begin();
  irsend.begin();
  ac.begin();
  mpuReady = initializeMpu6050();

  drawMainMenu();
  Serial.println("UNIVERSAL REMOTE READY");
#if GESTURE_MODEL_GENERATED
  Serial.println(USE_KNN_MODEL ? "Gesture model: KNN" : "Gesture model: Decision Tree");
#else
  Serial.println("Gesture model: fallback tilt logic (install a trained model for ML)");
#endif
  if (!mpuReady) Serial.println("WARNING: MPU6050 not detected; buttons remain available");
}

void loop() {
  updateButtons();
  updateGestureSampling();

  if (currentPage == SUB_MENU && millis() - lastActionTime > RETURN_TIME_MS) {
    returnToMainMenu();
    lastActionTime = millis();
  }
}
