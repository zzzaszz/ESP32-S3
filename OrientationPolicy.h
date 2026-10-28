#pragma once
#include <stdint.h>

// Confirmed on the installed board: its sensor sign is opposite to the LCD's
// upright direction. INVERT remains an optional correction on top of this.
inline bool Orientation_Output(bool detected, bool invert) { return detected ^ invert ^ true; }

// Pure policy, shared by the live sensor and the on-device regression test.
class OrientationPolicy {
 public:
  bool update(int x, int y, int z, uint32_t now, bool touching, bool &reversed) {
    const int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y, az = z < 0 ? -z : z;
    const int64_t magnitude = int64_t(x) * x + int64_t(y) * y + int64_t(z) * z;
    const int dominant = ax >= ay ? ax : ay, other = ax >= ay ? ay : ax;
    // Support either PCB axis on a tilted stand. Near diagonal boundaries,
    // flat placement, free fall, shaking and finger gestures hold direction.
    if (touching || magnitude < 650LL * 650 || magnitude > 1350LL * 1350 ||
        dominant < 400 || dominant * 4 < other * 5 || dominant * 4 < az * 2) {
      pending = false;
      return false;
    }
    const bool next = (ax >= ay ? x : y) < 0;
    if (!pending || candidate != next) {
      candidate = next;
      startedAt = now;
      pending = true;
      return false;
    }
    if (uint32_t(now - startedAt) < 700 || reversed == next) return false;
    reversed = next;
    return true;
  }
  void reset() { pending = false; }
 private:
  bool pending = false, candidate = false;
  uint32_t startedAt = 0;
};

// Gravity cannot observe yaw while the panel lies flat. Integrate the gyro
// about the screen normal relative to its startup/last upright direction.
class FlatRotationPolicy {
 public:
  bool update(int x, int y, int z, float zDps, uint32_t now, bool touching, bool &reversed) {
    const int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y, az = z < 0 ? -z : z;
    const int64_t magnitude = int64_t(x) * x + int64_t(y) * y + int64_t(z) * z;
    if (touching || az < 750 || ax >= 400 || ay >= 400 ||
        magnitude < 650LL * 650 || magnitude > 1350LL * 1350 ||
        zDps < -500.0f || zDps > 500.0f) {
      reset();
      return false;
    }
    const uint32_t elapsed = uint32_t(now - previousAt);
    previousAt = now;
    if (!havePrevious || elapsed > 100) {
      havePrevious = true;
      angle = 0;
      return false;
    }
    // A dead zone suppresses stationary bias; no heading is inferred at boot.
    if (zDps > -3.0f && zDps < 3.0f) return false;
    angle += zDps * elapsed / 1000.0f;
    if (angle >= 150.0f || angle <= -150.0f) {
      reversed = !reversed;
      angle += angle > 0 ? -180.0f : 180.0f;
      return true;
    }
    return false;
  }
  void reset() { havePrevious = false; angle = 0; }
  float angleDegrees() const { return angle; }
 private:
  bool havePrevious = false;
  uint32_t previousAt = 0;
  float angle = 0;
};

inline void Orientation_MapTouch(int rawX, int rawY, bool reversed, int &x, int &y) {
  rawX = rawX < 0 ? 0 : rawX > 239 ? 239 : rawX;
  rawY = rawY < 0 ? 0 : rawY > 319 ? 319 : rawY;
  x = reversed ? 319 - rawY : rawY;
  y = reversed ? rawX : 239 - rawX;
}
