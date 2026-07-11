#include "solide/battery.h"
#include "solide/board.h"

#include <Arduino.h>

// solide::battery — see battery.h. analogReadMilliVolts() uses the chip's eFuse
// ADC calibration, so the divider ratio is the only board-specific constant.

namespace solide {
namespace battery {

static bool s_present = false;

static uint16_t rawPackMv(uint8_t n) {
  const Board& b = board();
  if (b.batt.sense < 0) return 0;
  if (n < 1) n = 1;
  if (n > 32) n = 32;
  uint32_t sum = 0;
  for (uint8_t i = 0; i < n; i++) sum += analogReadMilliVolts(b.batt.sense);
  const uint32_t nodeMv = sum / n;
  return (uint16_t)((nodeMv * b.batt.dividerX100) / 100);
}

bool begin() {
  const Board& b = board();
  s_present = false;
  if (b.batt.sense < 0) return false;
  analogReadResolution(12);
  analogSetPinAttenuation(b.batt.sense, ADC_11db);   // full ~0–3.1 V span
  // Fitted-divider plausibility probe: a floating ADC pin reads noise (usually
  // near 0 or wildly unstable); a real pack sits at 2.5–4.4 V per cell.
  const uint16_t cells = b.batt.cells ? b.batt.cells : 1;
  const uint16_t cell = rawPackMv(8) / cells;
  s_present = (cell >= 2500 && cell <= 4400);
  return s_present;
}

bool present() { return s_present; }

uint16_t packMv(uint8_t n) { return s_present ? rawPackMv(n) : 0; }

uint16_t cellMv(uint8_t n) {
  const Board& b = board();
  const uint16_t cells = b.batt.cells ? b.batt.cells : 1;
  return s_present ? (uint16_t)(rawPackMv(n) / cells) : 0;
}

}  // namespace battery
}  // namespace solide
