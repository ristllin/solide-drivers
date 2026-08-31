// Host unit tests for solide::touch::Liveness - the portable FT6336U recovery
// policy (CUM-248). Drives the plan()/report() protocol exactly as the device
// layer does, asserting the failure counting, the no-finger vs fault
// distinction, the recovery ladder, counter reset, and exponential backoff.
#include <unity.h>
#include "solide/touch_liveness.h"

using solide::touch::Liveness;
using solide::touch::Health;
using Step = solide::touch::Liveness::Step;

void setUp() {}
void tearDown() {}

// Feed one healthy or faulty Attempt at time `t`. Returns the step that was
// planned (always Attempt while Normal).
static Step attempt(Liveness& l, bool ok, uint32_t t) {
  Step s = l.plan(t);
  l.report(s, ok, t);
  return s;
}

// --- Failure counting --------------------------------------------------------
static void test_counts_consecutive_failures() {
  Liveness l;   // default K=4
  uint32_t t = 0;
  for (int i = 1; i <= 3; i++) {
    attempt(l, false, t += 20);
    TEST_ASSERT_EQUAL_UINT16(i, l.health().consecutiveFailures);
    TEST_ASSERT_EQUAL_UINT32(i, l.health().failures);
  }
  // Below K the machine is not yet degraded and plans a plain Attempt.
  TEST_ASSERT_FALSE(l.health().degraded);
  TEST_ASSERT_EQUAL(Step::Attempt, l.plan(t));
}

// --- No-finger (ok, 0 points) is NOT a failure -------------------------------
static void test_no_finger_is_not_a_failure() {
  Liveness l;
  uint32_t t = 0;
  // A successful transaction that reports zero points is reported as ok=true by
  // the device layer (the fault/no-finger distinction lives there). Many such
  // "no touch" polls must never accrue a failure or trip recovery.
  for (int i = 0; i < 50; i++) attempt(l, true, t += 20);
  Health h = l.health();
  TEST_ASSERT_EQUAL_UINT32(0, h.failures);
  TEST_ASSERT_EQUAL_UINT16(0, h.consecutiveFailures);
  TEST_ASSERT_EQUAL_UINT32(0, h.recoveries);
  TEST_ASSERT_FALSE(h.degraded);
}

// --- A lone glitch clears on the next good read, no recovery counted ---------
static void test_transient_fault_clears_without_recovery() {
  Liveness l;
  uint32_t t = 0;
  attempt(l, true, t += 20);
  attempt(l, false, t += 20);   // single glitch (streak 1, < K)
  TEST_ASSERT_EQUAL_UINT16(1, l.health().consecutiveFailures);
  attempt(l, true, t += 20);    // recovered on its own
  Health h = l.health();
  TEST_ASSERT_EQUAL_UINT16(0, h.consecutiveFailures);
  TEST_ASSERT_EQUAL_UINT32(1, h.failures);      // the glitch was still counted
  TEST_ASSERT_EQUAL_UINT32(0, h.recoveries);    // but it was never a real outage
  TEST_ASSERT_FALSE(h.degraded);
}

// --- Recovery trigger: K faults -> bus-clear, then hard-reset ----------------
static void test_ladder_engages_after_k_faults() {
  Liveness l;   // K=4
  uint32_t t = 0;
  for (int i = 0; i < 4; i++) attempt(l, false, t += 20);
  TEST_ASSERT_TRUE(l.health().degraded);
  // First rung is a bus-clear.
  TEST_ASSERT_EQUAL(Step::BusClear, l.plan(t += 20));
  l.report(Step::BusClear, false, t);           // bus-clear did not restore comms
  TEST_ASSERT_EQUAL_UINT32(1, l.health().busClears);
  // Escalates to a hard reset.
  TEST_ASSERT_EQUAL(Step::HardReset, l.plan(t += 20));
  l.report(Step::HardReset, true, t);           // hard reset brought it back
  Health h = l.health();
  TEST_ASSERT_EQUAL_UINT32(1, h.hardResets);
  TEST_ASSERT_EQUAL_UINT32(1, h.recoveries);
  TEST_ASSERT_EQUAL_UINT32(t, h.lastRecoveryMs);
  TEST_ASSERT_FALSE(h.degraded);
  TEST_ASSERT_EQUAL_UINT16(0, h.consecutiveFailures);
  // Back to normal operation.
  TEST_ASSERT_EQUAL(Step::Attempt, l.plan(t += 20));
}

// --- Bus-clear alone can recover (no hard reset needed) ----------------------
static void test_bus_clear_recovers() {
  Liveness l;
  uint32_t t = 0;
  for (int i = 0; i < 4; i++) attempt(l, false, t += 20);
  TEST_ASSERT_EQUAL(Step::BusClear, l.plan(t += 20));
  l.report(Step::BusClear, true, t);            // clearing the wedged SDA was enough
  Health h = l.health();
  TEST_ASSERT_EQUAL_UINT32(1, h.busClears);
  TEST_ASSERT_EQUAL_UINT32(0, h.hardResets);
  TEST_ASSERT_EQUAL_UINT32(1, h.recoveries);
  TEST_ASSERT_FALSE(h.degraded);
}

// --- Counter reset: a good read after recovery starts a clean streak ---------
static void test_counter_resets_on_success() {
  Liveness l;
  uint32_t t = 0;
  for (int i = 0; i < 4; i++) attempt(l, false, t += 20);
  l.plan(t += 20);
  l.report(Step::BusClear, true, t);            // recover
  // New faults start counting from zero again.
  attempt(l, false, t += 20);
  TEST_ASSERT_EQUAL_UINT16(1, l.health().consecutiveFailures);
  attempt(l, true, t += 20);
  TEST_ASSERT_EQUAL_UINT16(0, l.health().consecutiveFailures);
}

// --- Exponential backoff after a full ladder miss ----------------------------
static void test_backoff_after_full_ladder_miss() {
  Liveness::Config cfg;
  cfg.failThreshold = 4;
  cfg.backoffBaseMs = 100;
  cfg.backoffMaxMs = 4000;
  Liveness l(cfg);
  uint32_t t = 1000;
  for (int i = 0; i < 4; i++) attempt(l, false, t += 20);

  // Miss the whole ladder once: bus-clear fails, hard reset fails -> backoff.
  TEST_ASSERT_EQUAL(Step::BusClear, l.plan(t += 20));
  l.report(Step::BusClear, false, t);
  TEST_ASSERT_EQUAL(Step::HardReset, l.plan(t += 20));
  l.report(Step::HardReset, false, t);
  uint32_t backoffStart = t;

  // Inside the 100 ms window every plan is Wait (no bus hammering).
  TEST_ASSERT_EQUAL(Step::Wait, l.plan(backoffStart + 50));
  TEST_ASSERT_EQUAL(Step::Wait, l.plan(backoffStart + 99));
  // After it, a plain Attempt is allowed.
  TEST_ASSERT_EQUAL(Step::Attempt, l.plan(backoffStart + 100));

  // Post-backoff probe still dead -> ladder again -> miss again -> backoff
  // doubles to 200 ms.
  t = backoffStart + 100;
  l.report(Step::Attempt, false, t);
  TEST_ASSERT_TRUE(l.health().degraded);
  TEST_ASSERT_EQUAL(Step::BusClear, l.plan(t += 20));
  l.report(Step::BusClear, false, t);
  TEST_ASSERT_EQUAL(Step::HardReset, l.plan(t += 20));
  l.report(Step::HardReset, false, t);
  uint32_t backoff2 = t;
  TEST_ASSERT_EQUAL(Step::Wait, l.plan(backoff2 + 199));
  TEST_ASSERT_EQUAL(Step::Attempt, l.plan(backoff2 + 200));
}

// --- Backoff is capped at the ceiling ----------------------------------------
static void test_backoff_caps_at_max() {
  Liveness::Config cfg;
  cfg.failThreshold = 1;      // trip the ladder on a single fault for a quick test
  cfg.backoffBaseMs = 1000;
  cfg.backoffMaxMs = 4000;
  Liveness l(cfg);
  uint32_t t = 0;

  // Repeatedly miss the full ladder; watch the backoff grow 1000->2000->4000->4000.
  const uint32_t expected[] = {1000, 2000, 4000, 4000};
  for (uint32_t exp : expected) {
    // Fault an Attempt to (re-)enter recovery.
    Step s = l.plan(t += 10);
    if (s == Step::Attempt) l.report(Step::Attempt, false, t);
    // Drive both rungs to failure.
    if (l.plan(t += 10) == Step::BusClear) l.report(Step::BusClear, false, t);
    if (l.plan(t += 10) == Step::HardReset) l.report(Step::HardReset, false, t);
    uint32_t start = t;
    TEST_ASSERT_EQUAL(Step::Wait, l.plan(start + exp - 1));
    TEST_ASSERT_EQUAL(Step::Attempt, l.plan(start + exp));
    t = start + exp;   // step past this backoff for the next iteration
  }
}

// --- A controller that comes back after backoff counts a recovery ------------
static void test_recovery_after_backoff_probe() {
  Liveness l;
  uint32_t t = 0;
  for (int i = 0; i < 4; i++) attempt(l, false, t += 20);
  l.plan(t += 20); l.report(Step::BusClear, false, t);
  l.plan(t += 20); l.report(Step::HardReset, false, t);
  uint32_t start = t;
  // Wait out the base backoff, then the controller answers.
  TEST_ASSERT_EQUAL(Step::Attempt, l.plan(start + 100));
  t = start + 100;
  l.report(Step::Attempt, true, t);
  Health h = l.health();
  TEST_ASSERT_EQUAL_UINT32(1, h.recoveries);
  TEST_ASSERT_FALSE(h.degraded);
  TEST_ASSERT_EQUAL_UINT16(0, h.consecutiveFailures);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_counts_consecutive_failures);
  RUN_TEST(test_no_finger_is_not_a_failure);
  RUN_TEST(test_transient_fault_clears_without_recovery);
  RUN_TEST(test_ladder_engages_after_k_faults);
  RUN_TEST(test_bus_clear_recovers);
  RUN_TEST(test_counter_resets_on_success);
  RUN_TEST(test_backoff_after_full_ladder_miss);
  RUN_TEST(test_backoff_caps_at_max);
  RUN_TEST(test_recovery_after_backoff_probe);
  return UNITY_END();
}
