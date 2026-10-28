#include "Touch_Driver.h"
#include "DisplayOrientation.h"
#include "OrientationPolicy.h"

#include <Arduino.h>
#include <Wire.h>
#include <assert.h>

namespace {

constexpr uint8_t kCst328Address = 0x1A;
constexpr uint8_t kCst3530Address = 0x58;
constexpr int kSdaPin = 1;
constexpr int kSclPin = 3;
constexpr int kResetPin = 2;
constexpr uint32_t kI2cFrequency = 400000;
constexpr uint8_t kMaxPoints = 5;

enum class Controller : uint8_t { None, Cst328, Cst3530 };

struct Point {
  uint16_t x = 0;
  uint16_t y = 0;
  uint16_t strength = 0;
};

static Controller controller = Controller::None;
static Point points[kMaxPoints];
static uint8_t pointCount = 0;
static lv_coord_t lastX = 0;
static lv_coord_t lastY = 0;
static bool pressed = false;

static bool readRegister(uint8_t address, uint32_t reg, uint8_t regBytes,
                         uint8_t *data, size_t length) {
  Wire1.beginTransmission(address);
  for (int shift = (regBytes - 1) * 8; shift >= 0; shift -= 8) {
    Wire1.write(static_cast<uint8_t>((reg >> shift) & 0xFF));
  }
  if (Wire1.endTransmission(false) != 0) return false;
  if (Wire1.requestFrom(static_cast<int>(address), static_cast<int>(length)) != length) return false;
  for (size_t i = 0; i < length; ++i) data[i] = Wire1.read();
  return true;
}

static bool writeRegister(uint8_t address, uint32_t reg, uint8_t regBytes,
                          const uint8_t *data, size_t length) {
  Wire1.beginTransmission(address);
  for (int shift = (regBytes - 1) * 8; shift >= 0; shift -= 8) {
    Wire1.write(static_cast<uint8_t>((reg >> shift) & 0xFF));
  }
  for (size_t i = 0; i < length; ++i) Wire1.write(data[i]);
  return Wire1.endTransmission(true) == 0;
}

static bool probe(uint8_t address) {
  Wire1.beginTransmission(address);
  return Wire1.endTransmission(true) == 0;
}

static void resetCst328() {
  pinMode(kResetPin, OUTPUT);
  digitalWrite(kResetPin, HIGH);
  delay(50);
  digitalWrite(kResetPin, LOW);
  delay(5);
  digitalWrite(kResetPin, HIGH);
  delay(50);
}

static void resetCst3530() {
  pinMode(kResetPin, OUTPUT);
  digitalWrite(kResetPin, LOW);
  delay(100);
  digitalWrite(kResetPin, HIGH);
  delay(500);
}

static bool detectCst328() {
  uint8_t info[24] = {};
  if (!writeRegister(kCst328Address, 0xD101, 2, nullptr, 0)) return false;
  if (!readRegister(kCst328Address, 0xD1F4, 2, info, sizeof(info))) return false;
  return static_cast<uint16_t>((info[11] << 8) | info[10]) == 0xCACA;
}

static bool readCst328() {
  uint8_t count = 0;
  if (!readRegister(kCst328Address, 0xD005, 2, &count, 1)) return false;
  const uint8_t touchCount = count & 0x0F;
  if (touchCount == 0) {
    const uint8_t clear = 0;
    writeRegister(kCst328Address, 0xD005, 2, &clear, 1);
    pointCount = 0;
    return true;
  }
  if (touchCount > kMaxPoints) return false;

  uint8_t raw[27] = {};
  if (!readRegister(kCst328Address, 0xD000, 2, raw, sizeof(raw))) return false;
  const uint8_t clear = 0;
  writeRegister(kCst328Address, 0xD005, 2, &clear, 1);
  pointCount = touchCount;
  for (uint8_t i = 0; i < pointCount; ++i) {
    const size_t offset = i * 5 + (i == 0 ? 1 : 3);
    points[i].x = static_cast<uint16_t>((raw[offset + 1] << 4) | (raw[offset + 3] >> 4));
    points[i].y = static_cast<uint16_t>((raw[offset + 2] << 4) | (raw[offset + 3] & 0x0F));
    points[i].strength = raw[offset + 4];
  }
  return true;
}

static bool readCst3530() {
  uint8_t raw[51] = {};
  if (!readRegister(kCst3530Address, 0xD0070000, 4, raw, 9)) return false;
  const uint8_t touchCount = raw[3] & 0x0F;
  if (touchCount == 0 || (raw[8] & 0xF0) == 0 || touchCount > kMaxPoints) {
    writeRegister(kCst3530Address, 0xD00002AB, 4, nullptr, 0);
    pointCount = 0;
    return true;
  }
  if (touchCount > 1 && !readRegister(kCst3530Address, 0xD0070900, 4, &raw[9], (touchCount - 1) * 5)) {
    writeRegister(kCst3530Address, 0xD00002AB, 4, nullptr, 0);
    return false;
  }
  writeRegister(kCst3530Address, 0xD00002AB, 4, nullptr, 0);
  pointCount = touchCount;
  for (uint8_t i = 0; i < pointCount; ++i) {
    const size_t offset = i * 5;
    points[i].x = static_cast<uint16_t>(((raw[offset + 7] & 0x0F) << 8) | raw[offset + 4]);
    points[i].y = static_cast<uint16_t>(((raw[offset + 7] & 0xF0) << 4) | raw[offset + 5]);
    points[i].strength = raw[offset + 6];
  }
  return true;
}

}  // namespace

bool TouchDriver_Init(void) {
  Wire1.begin(kSdaPin, kSclPin, kI2cFrequency);
  pinMode(4, INPUT);
  resetCst328();

  if (probe(kCst328Address) && detectCst328()) {
    controller = Controller::Cst328;
    Serial.println("TOUCH CST328 READY");
    return true;
  }
  resetCst3530();
  if (probe(kCst3530Address)) {
    controller = Controller::Cst3530;
    Serial.println("TOUCH CST3530 READY");
    return true;
  }
  controller = Controller::None;
  Serial.println("TOUCH NOT FOUND");
  return false;
}

void TouchDriver_Read(lv_indev_drv_t *, lv_indev_data_t *data) {
  bool ok = false;
  if (controller == Controller::Cst328) ok = readCst328();
  else if (controller == Controller::Cst3530) ok = readCst3530();

  if (ok && pointCount > 0) {
    // Touch controllers report the physical portrait coordinates. The LCD is
    // rotated 90 degrees by MADCTL for the landscape UI.
    int x, y;
    Orientation_MapTouch(points[0].x, points[0].y, DisplayOrientation_IsReversed(), x, y);
    lastX = x;
    lastY = y;
    pressed = true;
    data->point.x = lastX;
    data->point.y = lastY;
    data->state = LV_INDEV_STATE_PR;
    pointCount = 0;
  } else {
    pressed = false;
    data->point.x = lastX;
    data->point.y = lastY;
    data->state = LV_INDEV_STATE_REL;
  }
}

bool TouchDriver_IsPressed(void) { return pressed; }
