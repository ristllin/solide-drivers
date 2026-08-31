#pragma once
#include <Wire.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

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

// --- Cross-task serialization for the shared bus (CUM-271) -------------------
// Arduino-ESP32's Wire has an internal per-bus lock that makes an individual
// beginTransmission..endTransmission transaction atomic, but it does NOT cover
// tearing the whole driver down: the touch liveness ladder does Wire.end() ->
// bit-bang -> Wire.begin() to clear a wedged bus, and that teardown can land in
// the middle of a codec register transaction on the other core. Since both the
// FT6336U touch controller and the ES8311 codec live on this one Wire, that is a
// use-after-free on the IDF i2c driver handle (or, best case, a garbled codec
// write). The touch driver's own "we only get here after the bus is already dead"
// rationale is false for the CUM-248 monitor-mode NACK the ladder exists for: the
// bus is electrically fine and the codec can be mid-transaction.
//
// This recursive mutex is the shared seam EVERY user of this Wire takes: the touch
// primitives (probe/read/power-config) and the recovery ladder (bus-clear/hard-
// reset) on one side, and the codec register read/write primitives on the other.
// Serializing at the primitive level (not just around the teardown) also closes
// the touch-read-vs-teardown and dump-vs-teardown windows, and lets the one-time
// begin() below run race-free. Recursive so a caller that already owns it (a codec
// re-init nested in another locked region; a probe nested in a hard reset) does not
// self-deadlock. Created lazily on first use via a function-local static so it is
// one object across all TUs (ODR). Lock order across the codebase is ALWAYS
// s_spkMutex (audio) -> i2cBusMutex; the touch side takes only i2cBusMutex, so
// there is no lock-ordering cycle.
inline SemaphoreHandle_t i2cBusMutex() {
  static SemaphoreHandle_t m = xSemaphoreCreateRecursiveMutex();
  return m;
}

// RAII guard for i2cBusMutex(). Scope it around any access to the shared Wire -
// a single register transaction, a structural teardown (Wire.end()/begin()), or
// the one-time begin() - so no two can overlap on the bus.
struct I2cBusLock {
  I2cBusLock() { xSemaphoreTakeRecursive(i2cBusMutex(), portMAX_DELAY); }
  ~I2cBusLock() { xSemaphoreGiveRecursive(i2cBusMutex()); }
  I2cBusLock(const I2cBusLock&) = delete;
  I2cBusLock& operator=(const I2cBusLock&) = delete;
};

inline void i2cEnsureBegun(int sda, int scl, uint32_t hz = 400000) {
  I2cBusLock lock;   // the `begun` flag + Wire.begin() must not race a concurrent caller
  static bool begun = false;
  if (begun) return;
  Wire.begin(sda, scl);
  Wire.setClock(hz);
  begun = true;
}

}  // namespace solide
