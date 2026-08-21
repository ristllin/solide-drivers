#pragma once
#include <Wire.h>

// ============================================================================
// Single owner for the shared I2C bus.
//
// On the all-in-one boards the capacitive touch controller (FT6336U) and the
// audio codec (ES8311) sit on ONE I2C bus (e.g. SDA 16 / SCL 15 on the
// Freenove). Whichever driver initialises first calls i2cEnsureBegun(); the
// other's call is a no-op. Never call Wire.begin() directly from a driver, or
// two drivers race to re-init the bus with (possibly) different pins.
//
// The function-local static lives in an inline function, so it is one object
// across every translation unit that includes this header (guaranteed by the
// ODR rule for inline functions) - a genuine single owner, not one-per-TU.
// ============================================================================

namespace solide {

inline void i2cEnsureBegun(int sda, int scl, uint32_t hz = 400000) {
  static bool begun = false;
  if (begun) return;
  Wire.begin(sda, scl);
  Wire.setClock(hz);
  begun = true;
}

}  // namespace solide
