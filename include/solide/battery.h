#pragma once
#include <stdint.h>

// ============================================================================
// solide::battery - pack-voltage sensing via an ADC1 resistor divider.
//
// Reads the eFuse-calibrated millivolt value on board().batt.sense (so raw
// counts never matter), undoes the divider, and reports PACK and PER-CELL
// millivolts. Deliberately voltage-only: state-of-charge curves, thresholds
// and degradation models are app policy (Nimbus lib/core nimbus::power), not
// board support. begin() is safe when the divider isn't fitted (sense = -1 or
// an implausible reading): present() stays false and samples read 0.
//
// ⚠ ADC1 pins only (GPIO 1–10 on the S3): ADC2 reads garbage while WiFi is up.
// The divider must keep the node ≤ ~3.1 V at max pack voltage (see
// docs/hardware.md - 220k/100k = ÷3.2 puts a 2S 8.4 V pack at 2.63 V).
// ============================================================================

namespace solide {
namespace battery {

// Configure the sense pin (12-bit, 11 dB attenuation) and take a probe read.
// Returns true when a divider appears FITTED: pin >= 0 and the probe reads a
// plausible per-cell voltage (~2.5–4.4 V). A floating pin or absent divider
// returns false; sampling then reports 0 (callers treat as no-battery).
bool begin();

// True when begin() judged the sense line fitted + plausible.
bool present();

// Averaged pack voltage in millivolts (divider undone). 0 when not present.
// n = samples to average (1..32; more = quieter, ~100 µs each).
uint16_t packMv(uint8_t n = 8);

// Per-cell millivolts: packMv / board().batt.cells. 0 when not present.
uint16_t cellMv(uint8_t n = 8);

}  // namespace battery
}  // namespace solide
