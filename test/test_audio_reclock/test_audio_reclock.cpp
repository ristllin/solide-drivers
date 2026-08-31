// Host unit tests for solide::audio::planReclock - the portable codec re-clock
// decision that prevents the CUM-272 use-after-free (a rate-change teardown
// deleting g_rx out from under an in-flight recording). Asserts every
// interleaving of (codecUp, rate change, rxActive) the device codecInit() faces.
#include <unity.h>
#include "solide/audio_reclock.h"

using solide::audio::planReclock;
using solide::audio::ReclockPlan;

void setUp() {}
void tearDown() {}

// --- Cold start: nothing up yet -> build fresh (no teardown) ------------------
static void test_not_up_builds_fresh() {
  TEST_ASSERT_EQUAL(ReclockPlan::Fresh, planReclock(false, 0, 16000, false));
  // rxActive is meaningless before the codec exists; still Fresh.
  TEST_ASSERT_EQUAL(ReclockPlan::Fresh, planReclock(false, 0, 24000, true));
}

// --- Same rate: cheap no-op, whether or not a recording is live --------------
static void test_same_rate_is_noop() {
  TEST_ASSERT_EQUAL(ReclockPlan::NoChange, planReclock(true, 16000, 16000, false));
  // The keep-alive win: a same-rate reopen never tears down, so concurrent
  // record+play at one rate (loopbackSelfTest's 16 kHz) stays safe.
  TEST_ASSERT_EQUAL(ReclockPlan::NoChange, planReclock(true, 16000, 16000, true));
}

// --- Rate change with RX idle -> safe to re-clock ----------------------------
static void test_rate_change_idle_reclocks() {
  TEST_ASSERT_EQUAL(ReclockPlan::Reclock, planReclock(true, 16000, 24000, false));
  TEST_ASSERT_EQUAL(ReclockPlan::Reclock, planReclock(true, 24000, 16000, false));
}

// --- THE BUG: rate change while a recording holds RX -> must refuse -----------
static void test_rate_change_while_recording_refuses() {
  // A 24 kHz beep during a 16 kHz STT capture must NOT tear down g_rx.
  TEST_ASSERT_EQUAL(ReclockPlan::RefuseRxBusy, planReclock(true, 16000, 24000, true));
  // Symmetric: any different requested rate while RX is busy is refused.
  TEST_ASSERT_EQUAL(ReclockPlan::RefuseRxBusy, planReclock(true, 24000, 8000, true));
}

// --- The refusal is exactly what a teardown would need; never Reclock+busy ----
static void test_never_reclocks_under_live_rx() {
  // Sweep a spread of current/requested pairs with RX live: every DIFFERENT-rate
  // case is RefuseRxBusy, every SAME-rate case is NoChange - Reclock never
  // appears while rxActive, which is the invariant that kills the UAF.
  const uint32_t rates[] = {8000, 16000, 22050, 24000, 44100, 48000};
  for (uint32_t cur : rates) {
    for (uint32_t req : rates) {
      ReclockPlan p = planReclock(true, cur, req, /*rxActive=*/true);
      if (req == cur) TEST_ASSERT_EQUAL(ReclockPlan::NoChange, p);
      else            TEST_ASSERT_EQUAL(ReclockPlan::RefuseRxBusy, p);
      TEST_ASSERT_FALSE(p == ReclockPlan::Reclock);
    }
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_not_up_builds_fresh);
  RUN_TEST(test_same_rate_is_noop);
  RUN_TEST(test_rate_change_idle_reclocks);
  RUN_TEST(test_rate_change_while_recording_refuses);
  RUN_TEST(test_never_reclocks_under_live_rx);
  return UNITY_END();
}
