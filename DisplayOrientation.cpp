#include "DisplayOrientation.h"
#include "OrientationPolicy.h"
#include "Display_ST7789.h"
#include "Touch_Driver.h"
#include "BatteryDashboard.h"
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <lvgl.h>
#include <string.h>

namespace {
OrientationPolicy policy;
FlatRotationPolicy flatPolicy;
Preferences settings;
bool settingsReady = false, sensorReady = false, busReady = false;
bool reversed = false, automaticReversed = false, inverted = false, sampleValid = false;
uint8_t mode = 0, address = 0; // 0 auto, 1 normal, 2 reversed
uint32_t lastPoll = 0, lastProbe = 0, lastSample = 0, readErrors = 0;
int accel[3] = {};
float gyroZ = 0;

bool readRegisters(uint8_t reg, uint8_t *data, size_t length) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(int(address), int(length)) != length) return false;
  for (size_t i = 0; i < length; ++i) data[i] = Wire.read();
  return true;
}

bool writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool probeSensor() {
  if (!busReady) return false;
  // Board pins and register configuration checked against Waveshare V2 demo.
  for (uint8_t candidate : {uint8_t(0x6B), uint8_t(0x6A)}) {
    address = candidate;
    uint8_t id = 0;
    if (!readRegisters(0x00, &id, 1) || id != 0x05) continue;
    // Auto-increment, accelerometer +/-4g at 60 Hz, gyro +/-512 dps at 120 Hz.
    if (!writeRegister(0x08, 0x00) || !writeRegister(0x02, 0x40) ||
        !writeRegister(0x03, 0x17) || !writeRegister(0x04, 0x56) ||
        !writeRegister(0x06, 0x11) || !writeRegister(0x08, 0x43)) continue;
    uint8_t config[2] = {};
    if (!readRegisters(0x03, config, 2) || config[0] != 0x17 || config[1] != 0x56) continue;
    Serial.printf("ORIENTATION QMI8658 READY address=0x%02X\n", address);
    return true;
  }
  return false;
}

void applyDirection(bool next) {
  if (next == reversed) return;
  // All display, touch and policy calls run on the same Arduino loop task.
  for (lv_indev_t *input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input)) {
    lv_indev_reset(input, nullptr);
    lv_indev_wait_release(input);
  }
  BatteryDashboard_PrepareRotation();
  LCD_SetReversed(next);
  reversed = next;
  BatteryDashboard_FinishRotation();
  Serial.printf("ORIENTATION CHANGED reversed=%u\n", reversed);
}

void applyMode() {
  applyDirection(mode == 0 ? Orientation_Output(automaticReversed, inverted) : mode == 2);
}

bool selfTest() {
  if (!Orientation_Output(false, false) || Orientation_Output(true, false) ||
      Orientation_Output(false, true) || !Orientation_Output(true, true)) return false;
  OrientationPolicy p;
  bool value = false;
  if (p.update(-1000, 0, 0, 0, false, value) || p.update(-1000, 0, 0, 699, false, value)) return false;
  if (!p.update(-1000, 0, 0, 700, false, value) || !value) return false;
  p.update(1000, 0, 0, 800, false, value);
  p.update(0, 0, 1000, 1500, false, value); // flat cancels pending change
  if (p.update(1000, 0, 0, 1600, false, value) || !value) return false;
  p.update(1000, 0, 0, 2300, true, value); // touch cancels pending change
  if (p.update(1000, 0, 0, 2400, false, value) || !value) return false;
  if (!p.update(1000, 0, 0, 3100, false, value) || value) return false;
  if (p.update(-2000, 0, 0, 4000, false, value)) return false;
  if (p.update(0, 0, 0, 4800, false, value)) return false;
  if (p.update(-400, 900, 0, 5600, false, value)) return false;
  p.reset();
  p.update(-1000, 0, 0, UINT32_MAX - 400, false, value);
  if (!p.update(-1000, 0, 0, 300, false, value)) return false;
  // Actual stand readings showed Y≈720mg/Z≈-770mg, not X-dominant.
  OrientationPolicy stand;
  bool standValue = false;
  stand.update(30, 720, -770, 0, false, standValue);
  if (stand.update(30, 720, -770, 700, false, standValue) || standValue) return false;
  stand.update(-30, -720, -770, 800, false, standValue);
  if (!stand.update(-30, -720, -770, 1500, false, standValue) || !standValue) return false;
  stand.update(30, 720, -770, 1600, false, standValue);
  if (!stand.update(30, 720, -770, 2300, false, standValue) || standValue) return false;
  stand.update(-650, 650, 0, 2400, false, standValue);
  if (stand.update(-650, 650, 0, 3100, false, standValue)) return false;
  FlatRotationPolicy flat;
  bool flatValue = false;
  flat.update(0, 0, -1000, 0, 0, false, flatValue);
  int flips = 0;
  for (uint32_t t = 20; t <= 2000; t += 20) {
    if (flat.update(0, 0, -1000, 90, t, false, flatValue)) ++flips;
    if (t <= 1000 && flatValue) return false; // 90 degrees must not flip
  }
  if (flips != 1 || !flatValue) return false;
  for (uint32_t t = 2020; t <= 4000; t += 20) {
    if (flat.update(0, 0, -1000, -90, t, false, flatValue)) ++flips;
  }
  if (flips != 2 || flatValue) return false;
  for (uint32_t t = 4020; t <= 14000; t += 20) {
    if (flat.update(0, 0, -1000, 2, t, false, flatValue)) return false;
  }
  if (flat.angleDegrees() > 1 || flat.angleDegrees() < -1) return false;
  // Touch, invalid pose and missed samples must cancel partial yaw.
  flat.update(0, 0, -1000, 90, 14020, true, flatValue);
  if (flat.angleDegrees() != 0) return false;
  flat.update(1000, 0, 0, 90, 14040, false, flatValue);
  if (flat.angleDegrees() != 0) return false;
  flat.update(0, 0, -1000, 90, 14060, false, flatValue);
  flat.update(0, 0, -1000, 90, 14200, false, flatValue);
  if (flat.angleDegrees() != 0) return false;
  for (int rx : {0, 239}) for (int ry : {0, 319}) {
    int x, y, oppositeX, oppositeY;
    Orientation_MapTouch(rx, ry, false, x, y);
    Orientation_MapTouch(rx, ry, true, oppositeX, oppositeY);
    if (x + oppositeX != 319 || y + oppositeY != 239) return false;
  }
  return true;
}
} // namespace

void DisplayOrientation_Init() {
  settingsReady = settings.begin("orientation", false);
  if (settingsReady) {
    mode = settings.getUChar("mode", 0);
    if (mode > 2) mode = 0;
    inverted = settings.getBool("invert", false);
  }
  busReady = Wire.begin(11, 10, 400000);
  Wire.setTimeOut(20);
  sensorReady = probeSensor();
  lastProbe = millis();
  if (!sensorReady) Serial.println("ORIENTATION QMI8658 UNAVAILABLE; retaining direction");
  applyMode();
}

void DisplayOrientation_Loop() {
  const uint32_t now = millis();
  if (!sensorReady) {
    if (uint32_t(now - lastProbe) < 5000) return;
    lastProbe = now;
    sensorReady = probeSensor();
    policy.reset();
    flatPolicy.reset();
    return;
  }
  if (uint32_t(now - lastPoll) < 20) return;
  lastPoll = now;
  uint8_t bytes[12];
  if (!readRegisters(0x35, bytes, sizeof(bytes))) {
    ++readErrors;
    sampleValid = false;
    sensorReady = false;
    lastProbe = now;
    policy.reset();
    flatPolicy.reset();
    return;
  }
  for (size_t i = 0; i < 3; ++i) {
    const int16_t raw = int16_t(uint16_t(bytes[2 * i]) | uint16_t(bytes[2 * i + 1]) << 8);
    accel[i] = int32_t(raw) * 1000 / 8192; // +/-4g scale -> milligravity
  }
  const int16_t rawGyroZ = int16_t(uint16_t(bytes[10]) | uint16_t(bytes[11]) << 8);
  gyroZ = rawGyroZ / 64.0f; // +/-512 dps: 64 LSB per degree/second
  sampleValid = true;
  lastSample = now;
  if (mode == 0) {
    const bool touching = TouchDriver_IsPressed();
    const bool tiltChanged = policy.update(accel[0], accel[1], accel[2], now, touching, automaticReversed);
    const bool flatChanged = flatPolicy.update(accel[0], accel[1], accel[2], gyroZ, now, touching, automaticReversed);
    if (tiltChanged || flatChanged) applyMode();
  }
}

void DisplayOrientation_PrintDiagnostics() {
  Serial.printf("ORIENTATION mode=%s reversed=%u invert=%u sensor=%s address=0x%02X ax=%d ay=%d az=%d valid=%u ageMs=%lu errors=%lu algorithm=accel+gyro gz=%.2f yaw=%.1f\n",
      mode == 0 ? "AUTO" : mode == 1 ? "NORMAL" : "REVERSED", reversed, inverted,
      sensorReady ? "ready" : "unavailable", address, accel[0], accel[1], accel[2], sampleValid,
      sampleValid ? (unsigned long)(millis() - lastSample) : 0UL, (unsigned long)readErrors, gyroZ, flatPolicy.angleDegrees());
}

bool DisplayOrientation_Command(const char *command) {
  if (strcmp(command, "ORIENTATION?") == 0) { DisplayOrientation_PrintDiagnostics(); return true; }
  if (strcmp(command, "ORIENTATION TEST") == 0) {
    Serial.println(selfTest() ? "ORIENTATION TEST PASS" : "ORIENTATION TEST FAIL");
    return true;
  }
  const bool setMode = strncmp(command, "ORIENTATION ", 12) == 0;
  const bool setInvert = strncmp(command, "ORIENTATION INVERT ", 19) == 0;
  if (!setMode) return false;
  uint8_t nextMode = mode;
  bool nextInvert = inverted;
  if (strcmp(command, "ORIENTATION AUTO") == 0) nextMode = 0;
  else if (strcmp(command, "ORIENTATION NORMAL") == 0) nextMode = 1;
  else if (strcmp(command, "ORIENTATION REVERSED") == 0) nextMode = 2;
  else if (strcmp(command, "ORIENTATION INVERT ON") == 0) nextInvert = true;
  else if (strcmp(command, "ORIENTATION INVERT OFF") == 0) nextInvert = false;
  else { Serial.println("ERR ORIENTATION FORMAT"); return true; }
  if (!settingsReady || (setInvert ? settings.putBool("invert", nextInvert) == 0
                                  : settings.putUChar("mode", nextMode) == 0)) {
    Serial.println("ERR ORIENTATION SAVE");
    return true;
  }
  mode = nextMode;
  inverted = nextInvert;
  policy.reset();
  flatPolicy.reset();
  applyMode();
  DisplayOrientation_PrintDiagnostics();
  return true;
}

bool DisplayOrientation_IsReversed() { return reversed; }
uint8_t DisplayOrientation_Mode() { return mode; }
