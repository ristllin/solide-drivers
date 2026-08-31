// Host unit tests for the ILI9341 panel-health compare (CUM-244) - the
// flip-aware RDDST readback check that decides whether the TFT still holds the
// configuration we wrote. The pure decision lives in solide/panel_health.h; the
// device layer (src/device/display_tft.cpp::healthy()) reads RDDST over SPI and
// delegates the verdict here, so these tests pin the EXACT logic that ships.
//
// This is the CUM-231 white-screen regression class. That bug came back when the
// compare mask was loosened 0xFE -> 0x3E to quiet a flip-toggle repaint thrash
// (CUM-188): dropping 0xC0 silently killed MY/MX fault detection, and the suite
// stayed green because nothing asserted a fault in the newly-ignored bits. So:
//
//   1. Counter-tests pin the CURRENT 0xFE mask: for EVERY checked bit, a
//      simulated RDDST fault in that bit must read unhealthy. Loosening the mask
//      makes a specific bit's row fail loudly instead of passing silently.
//   2. An anti-thrash test walks a legitimate flip toggle sequence and asserts
//      the health verdict never flaps - heal-count AND transition-count stay 0
//      over N cycles (the CUM-188 oscillation, counted not just final-state).
//   3. The heal-CLASSIFICATION report shape (CUM-186: healthy=1 heals=0) is NOT
//      host-reachable from this driver - see the bench-leg spec at the bottom.
#include <unity.h>
#include <cstdio>
#include "solide/panel_health.h"

using namespace solide::display_tft;

void setUp() {}
void tearDown() {}

// Model of RDDST's top byte on a HEALTHY panel, from the hardware measurements
// documented in panel_health.h / display_tft.cpp: the fixed MADCTL bits mirror
// what we wrote, the MY/MX flip bits (0xC0) always read back as their power-on 0
// regardless of the flip, and bit0 is the refresh scan-direction toggle. This is
// what the device would feed panelHealthy() when nothing is wrong.
static uint8_t healthyRddst(bool flip, bool refreshToggle) {
  uint8_t v = uint8_t(madctlFor(flip) & ~kMadctlFlipBits);  // MY/MX report as 0
  if (refreshToggle) v |= 0x01;                             // bit0 flips on refresh
  return v;
}

// --- The mask is exactly what we think it is ---------------------------------
// A tripwire: if someone edits the constant, this states the intended value out
// loud so the change is deliberate, and it documents which bits the counter-test
// table below is expected to cover.
static void test_compare_mask_is_0xFE() {
  TEST_ASSERT_EQUAL_HEX8(0xFE, kHealthCompareMask);
  // The expected readback is 0x28 for BOTH orientations (flip bits cleared on the
  // expected side, not masked out of the compare - that distinction is the fix).
  TEST_ASSERT_EQUAL_HEX8(0x28, expectedRddst(false));
  TEST_ASSERT_EQUAL_HEX8(0x28, expectedRddst(true));
}

// --- Positive controls: a healthy readback passes in both orientations -------
static void test_healthy_readback_passes_both_flips() {
  for (int flip = 0; flip <= 1; flip++) {
    TEST_ASSERT_TRUE(panelHealthy(healthyRddst(flip, false), flip));
    TEST_ASSERT_TRUE(panelHealthy(healthyRddst(flip, true), flip));  // bit0 set too
  }
}

// --- bit0 is the ONE bit that must NOT affect the verdict ---------------------
// The refresh scan-direction toggle flips during normal refresh on a perfectly
// healthy panel; treating it as a fault would be its own thrash source.
static void test_bit0_refresh_toggle_is_ignored() {
  for (int flip = 0; flip <= 1; flip++) {
    const uint8_t base = uint8_t(expectedRddst(flip));       // 0x28
    TEST_ASSERT_TRUE(panelHealthy(base, flip));              // bit0 = 0
    TEST_ASSERT_TRUE(panelHealthy(uint8_t(base | 0x01), flip));  // bit0 = 1
  }
}

// --- COUNTER-TESTS: every bit in the 0xFE mask must be fault-detecting --------
// Table-driven over all 7 pinned bits (1..7; bit0 is the ignored refresh
// toggle). For each bit, start from a healthy readback and flip JUST that bit -
// a simulated partial state loss in that dimension - and require the verdict to
// go unhealthy, in BOTH orientations. Bits 6 and 7 are MY/MX (0xC0): those are
// the bits the CUM-231 loosened mask (0x3E) dropped, so those rows are the exact
// regression guard - reintroducing that mask makes them fail here.
static void test_counter_every_masked_bit_detects_a_fault() {
  for (uint8_t bit = 1; bit <= 7; bit++) {          // bit0 deliberately excluded
    const uint8_t faultBit = uint8_t(1u << bit);
    TEST_ASSERT_TRUE_MESSAGE((kHealthCompareMask & faultBit) != 0,
                             "bit claimed to be in the mask is not - table/mask drift");
    for (int flip = 0; flip <= 1; flip++) {
      const uint8_t faulted = uint8_t(healthyRddst(flip, false) ^ faultBit);
      char msg[64];
      snprintf(msg, sizeof msg, "fault in bit %u (0x%02X), flip=%d slipped through",
               bit, faultBit, flip);
      TEST_ASSERT_FALSE_MESSAGE(panelHealthy(faulted, flip), msg);
    }
  }
}

// --- CUM-231 regression, stated directly --------------------------------------
// The specific fault the loosened mask waved through: a partial state loss that
// RAISES the MY/MX bits in the readback. The current flip-aware compare catches
// it in both orientations; a mask that dropped 0xC0 would not.
static void test_my_mx_partial_loss_is_caught() {
  for (int flip = 0; flip <= 1; flip++) {
    const uint8_t raisedMY = uint8_t(healthyRddst(flip, false) | 0x80);  // MY
    const uint8_t raisedMX = uint8_t(healthyRddst(flip, false) | 0x40);  // MX
    TEST_ASSERT_FALSE(panelHealthy(raisedMY, flip));
    TEST_ASSERT_FALSE(panelHealthy(raisedMX, flip));
  }
  // Proof the mask, not luck, catches it: the CUM-231 mask (0x3E) would MISS a
  // raised-MY/MX readback - it drops 0xC0 - so the loosened compare reads
  // healthy where the shipped 0xFE compare reads unhealthy. This asserts the
  // counter-test above has teeth against that exact regression.
  const uint8_t loosened = 0x3E;
  const uint8_t raisedMY = uint8_t(healthyRddst(false, false) | 0x80);
  TEST_ASSERT_TRUE((raisedMY & loosened) == (expectedRddst(false) & loosened));  // MISSED
  TEST_ASSERT_FALSE(panelHealthy(raisedMY, false));                              // CAUGHT
}

// --- A silently reset panel (0x00) still fails --------------------------------
static void test_reset_panel_reads_unhealthy() {
  TEST_ASSERT_FALSE(panelHealthy(0x00, false));
  TEST_ASSERT_FALSE(panelHealthy(0x00, true));
}

// --- ANTI-THRASH: a legitimate flip toggle must not flap the health verdict ---
// The CUM-188 oscillation was a health check that flapped to reach its end
// state; a terminal-value assertion hides it. Walk a flip toggle sequence,
// reading health several times per orientation with the refresh bit alternating,
// and count BOTH the unhealthy verdicts (each would trigger a heal/rearm) and
// the number of times the verdict changes (transitions). A correct flip-aware
// compare holds healthy throughout: 0 heals, 0 transitions, over N cycles.
static void test_flip_toggle_does_not_thrash() {
  const int kCycles = 32;
  bool flip = false;
  int heals = 0;
  int transitions = 0;
  bool prev = panelHealthy(healthyRddst(flip, false), flip);
  TEST_ASSERT_TRUE(prev);   // start healthy

  for (int c = 0; c < kCycles; c++) {
    flip = !flip;                                   // legitimate 180-degree toggle
    for (int r = 0; r < 4; r++) {                   // several polls per orientation
      const bool h = panelHealthy(healthyRddst(flip, (r & 1) != 0), flip);
      if (!h) heals++;
      if (h != prev) transitions++;
      prev = h;
    }
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, heals, "flip toggle drove a heal - CUM-231 thrash");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, transitions,
                                "health verdict oscillated over flip toggles - CUM-188");
}

// --- The anti-thrash assertion is not vacuous ---------------------------------
// Sentinel: the PRE-fix compare (compare MY/MX on both sides, i.e. expect
// madctlFor(flip) itself over the full 0xFE mask) is exactly the CUM-188 thrash -
// a flipped panel whose RDDST reports MY/MX as 0 reads unhealthy, so toggling
// flip flaps the verdict. If our test harness could not tell that apart from the
// shipped compare, the bounded-transition assertion above would prove nothing.
static bool buggyPreFixCompare(uint8_t rddstTop, bool flip) {
  // compares the flip bits instead of clearing them on the expected side
  return uint8_t(rddstTop & kHealthCompareMask) == uint8_t(madctlFor(flip) & kHealthCompareMask);
}
static void test_prefix_compare_would_oscillate() {
  int transitions = 0;
  bool flip = false;
  bool prev = buggyPreFixCompare(healthyRddst(flip, false), flip);
  for (int c = 0; c < 8; c++) {
    flip = !flip;
    const bool h = buggyPreFixCompare(healthyRddst(flip, false), flip);
    if (h != prev) transitions++;
    prev = h;
  }
  // The buggy compare flaps every toggle; the shipped one (asserted above) does
  // not. This is the teeth behind test_flip_toggle_does_not_thrash.
  TEST_ASSERT_GREATER_THAN_INT(0, transitions);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_compare_mask_is_0xFE);
  RUN_TEST(test_healthy_readback_passes_both_flips);
  RUN_TEST(test_bit0_refresh_toggle_is_ignored);
  RUN_TEST(test_counter_every_masked_bit_detects_a_fault);
  RUN_TEST(test_my_mx_partial_loss_is_caught);
  RUN_TEST(test_reset_panel_reads_unhealthy);
  RUN_TEST(test_flip_toggle_does_not_thrash);
  RUN_TEST(test_prefix_compare_would_oscillate);
  return UNITY_END();
}

// ============================================================================
// Scope item 3 - heal-CLASSIFICATION report shape (CUM-186): NOT host-reachable
// from this driver, so it is spec'd as a bench leg rather than faked here.
//
// Why not host-reachable: this driver exposes only the raw predicate
// solide::display_tft::healthy() (delegating to panelHealthy above) and
// reinit()/rearm(). It keeps NO heal counter and emits NO health/heal report.
// The "healthy=1 heals=0" discrepancy of CUM-186 is a property of the CONSUMER's
// render/rearm watchdog in the nimbus firmware (it counts rearm/heal actions and
// reports them alongside the healthy() reading); the seam that could ever show
// "reported healthy while a heal fired" does not exist on this side of the
// contract. Faking a heal counter in the driver test would assert a shape the
// driver does not own - green here would mean nothing for the real regression.
//
// Bench leg (owned by the nimbus consumer's watchdog tests, cross-referenced on
// CUM-244 / CUM-186):
//   Given a panel driven by this driver, over a run with NO induced fault
//   (healthy() true throughout), the consumer's health telemetry must report
//   heals/rearms = 0. Any heal fired while healthy() reads true is the CUM-186
//   discrepancy. The driver-side guarantee this rests on is pinned above: a
//   legitimate flip toggle never makes healthy() read false
//   (test_flip_toggle_does_not_thrash), so the consumer watchdog has no
//   driver-side reason to heal during a healthy flip.
// ============================================================================
