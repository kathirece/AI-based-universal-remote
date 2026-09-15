#include <Wire.h>

// MPU6050 gesture dataset collector for ESP32.
// Open Serial Monitor at 115200 baud and send:
//   u = UP, d = DOWN, l = LEFT, r = RIGHT
// Perform one gesture immediately after sending the command.

constexpr uint8_t MPU6050_ADDRESS = 0x68;
constexpr uint8_t MPU6050_ACCEL_START = 0x3B;
constexpr uint8_t SDA_PIN = 21;
constexpr uint8_t SCL_PIN = 22;
constexpr uint16_t SAMPLE_INTERVAL_MS = 50;
constexpr uint8_t SAMPLES_PER_CAPTURE = 20;

uint32_t captureId = 0;

void writeMpuRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MPU6050_ADDRESS);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission(true);
}

bool initializeMpu6050() {
  writeMpuRegister(0x6B, 0x00);  // Wake the sensor.
  delay(100);
  writeMpuRegister(0x1C, 0x00);  // Accelerometer range: +/-2 g.
  writeMpuRegister(0x1A, 0x03);  // Low-pass filter setting.

  Wire.beginTransmission(MPU6050_ADDRESS);
  Wire.write(0x75);  // WHO_AM_I register.
  if (Wire.endTransmission(false) != 0) return false;
  Wire.requestFrom(MPU6050_ADDRESS, static_cast<uint8_t>(1), true);
  return Wire.available() && Wire.read() == 0x68;
}

bool readAcceleration(float &ax, float &ay, float &az) {
  Wire.beginTransmission(MPU6050_ADDRESS);
  Wire.write(MPU6050_ACCEL_START);
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

const char *labelFromCommand(char command) {
  switch (command) {
    case 'u': return "UP";
    case 'd': return "DOWN";
    case 'l': return "LEFT";
    case 'r': return "RIGHT";
    default: return nullptr;
  }
}

void captureGesture(const char *label) {
  ++captureId;
  uint32_t nextSampleTime = millis();

  for (uint8_t sample = 0; sample < SAMPLES_PER_CAPTURE; ++sample) {
    while (static_cast<int32_t>(millis() - nextSampleTime) < 0) delay(1);

    float ax, ay, az;
    if (!readAcceleration(ax, ay, az)) {
      Serial.println("# ERROR: MPU6050 read failed");
      return;
    }

    Serial.print(label);
    Serial.print(',');
    Serial.print(captureId);
    Serial.print(',');
    Serial.print(sample);
    Serial.print(',');
    Serial.print(ax, 6);
    Serial.print(',');
    Serial.print(ay, 6);
    Serial.print(',');
    Serial.println(az, 6);

    nextSampleTime += SAMPLE_INTERVAL_MS;
  }

  Serial.print("# Capture complete: ");
  Serial.println(label);
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);

  if (!initializeMpu6050()) {
    Serial.println("# ERROR: MPU6050 not found at I2C address 0x68");
    while (true) delay(1000);
  }

  Serial.println("label,capture_id,sample_index,ax,ay,az");
  Serial.println("# Ready. Send u, d, l, or r, then perform the gesture.");
}

void loop() {
  if (!Serial.available()) return;

  char command = static_cast<char>(Serial.read());
  if (command >= 'A' && command <= 'Z') command += ('a' - 'A');
  const char *label = labelFromCommand(command);
  if (label != nullptr) captureGesture(label);
}
