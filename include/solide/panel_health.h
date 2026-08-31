#pragma once
#include <cstdint>

// ============================================================================
// Portable, host-testable panel-health compare for the ILI9341 TFT (CUM-244).
// The hardware glue (the RDDST read over the shared SPI bus, the flip write)
// lives in src/device/display_tft.cpp; the DECISION - "does this readback prove
// the panel still holds the configuration we wrote" - lives here as pure
// functions so `pio test -e native` can pin it. display_tft.cpp delegates to
// these, so there is ONE source of truth for the MADCTL byte and the compare
// mask, not two that can drift.
//
// Why this seam exists (CUM-231, the white-screen regression class): the
// compare mask was once loosened 0xFE -> 0x3E to quiet a flip-toggle repaint
// thrash (CUM-188). That silently masked out the MY/MX flip bits, dropping the
// exact fault detection that catches a partial state loss - and the suite
// stayed green because nothing asserted a fault in the newly-ignored bits, so
// the white screen came back. v0.7.2 restored the full 0xFE mask with a
// flip-AWARE expected value (MY/MX cleared on the expected side, not masked out
// of the compare). The counter-tests in test/test_panel_health pin every bit of
// this mask: loosening it again makes a specific bit's test fail loudly instead
// of passing silently. The rule is written in nimbus AGENTS.md section 4.
// ============================================================================

namespace solide::display_tft {

// The MADCTL byte we WRITE for each mounting orientation. MV (0x20) rotates the
// native portrait surface to landscape; BGR (0x08) matches these red BGR-wired
// modules; the 180-degree flip adds MY|MX (0xC0). Single source of truth for
// both the init/rearm write and the health compare below.
constexpr uint8_t kMadctlBgrLandscape = 0x28;   // MV | BGR, default (not flipped)
constexpr uint8_t kMadctlFlipBits     = 0xC0;   // MY | MX - the 180-degree turn

inline uint8_t madctlFor(bool flip) {
  return flip ? uint8_t(kMadctlBgrLandscape | kMadctlFlipBits) : kMadctlBgrLandscape;
}

// Bits of RDDST's top byte the health compare checks. bit0 (0x01) is the refresh
// scan-direction toggle - it flips during refresh on a perfectly healthy panel,
// so it is the ONE bit dropped. Everything else (0xFE) is pinned: BGR, MV, the
// refresh-order bits, AND the MY/MX flip bits. Loosening this (e.g. back to the
// CUM-231 0x3E, which drops 0xC0) MUST break a counter-test, never pass quietly.
constexpr uint8_t kHealthCompareMask = 0xFE;

// The flip-aware expected RDDST readback. RDDST's top byte mirrors MADCTL's
// fixed bits, but this panel reports the MY/MX flip bits (0xC0) as their
// power-on 0 REGARDLESS of the flip we wrote (measured on hardware, CUM-188). So
// the expected value is madctlFor(flip) with the flip bits CLEARED - 0x28 for
// both orientations. Clearing them on the EXPECTED side (rather than masking
// 0xC0 out of both sides) is what keeps a flipped panel from reading unhealthy
// forever - no watchdog thrash - while STILL catching a partial state loss that
// RAISES MY/MX in the readback (that is the CUM-231 fault the loosened mask let
// through).
inline uint8_t expectedRddst(bool flip) {
  return uint8_t(madctlFor(flip) & ~kMadctlFlipBits);   // 0x28 for both flips
}

// The pure panel-health verdict. `rddstTop` is RDDST's top byte
// (readReg(0x09, 4) >> 24 on the device); `flip` is the orientation last
// written. True when the panel still holds the configuration we set. A silently
// reset panel reverts to 0x00 and fails; a partial loss that flips any pinned
// bit fails; a legitimate flip toggle does NOT (both orientations expect 0x28).
inline bool panelHealthy(uint8_t rddstTop, bool flip) {
  return uint8_t(rddstTop & kHealthCompareMask) == expectedRddst(flip);
}

}  // namespace solide::display_tft
