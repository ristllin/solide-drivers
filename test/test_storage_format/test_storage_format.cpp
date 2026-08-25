// Host unit tests for solide::storage::format()'s state machine + guards.
//
// format() itself calls FATFS f_mkfs and the SD/SD_MMC Arduino backends, none of
// which exist on the host. The part that a wrong change would silently break -
// the refuse-when-absent guard and the mkfs-then-remount ordering - lives in the
// portable detail::runFormat() (storage_format.h), which these tests drive with
// fake device steps. No hardware, no Arduino.
#include <unity.h>
#include "solide/storage_format.h"

using solide::storage::FormatResult;
using solide::storage::formatResultStr;
using solide::storage::detail::runFormat;

// Records how the injected steps were exercised, so tests can assert not just the
// result but that the wrong step was never taken (e.g. mkfs on an absent card).
struct Trace {
  int  mkfsCalls = 0;
  int  remountCalls = 0;
  bool mkfsBeforeRemount = true;  // false if remount ever ran before mkfs
};

void setUp() {}
void tearDown() {}

// Guard: no card mounted -> NoCard, and mkfs/remount are NEVER invoked. This is
// the "refuse cleanly when no card is mounted" contract: it must not touch the
// card it does not have.
static void test_no_card_refuses_without_touching() {
  Trace t;
  FormatResult r = runFormat(
      /*mounted=*/false,
      [&] { t.mkfsCalls++; return true; },
      [&] { t.remountCalls++; return true; });
  TEST_ASSERT_EQUAL(FormatResult::NoCard, r);
  TEST_ASSERT_EQUAL_INT(0, t.mkfsCalls);
  TEST_ASSERT_EQUAL_INT(0, t.remountCalls);
}

// mkfs fails -> MkfsFailed, and remount is NOT attempted on a failed format.
static void test_mkfs_failure_skips_remount() {
  Trace t;
  FormatResult r = runFormat(
      /*mounted=*/true,
      [&] { t.mkfsCalls++; return false; },
      [&] { t.remountCalls++; return true; });
  TEST_ASSERT_EQUAL(FormatResult::MkfsFailed, r);
  TEST_ASSERT_EQUAL_INT(1, t.mkfsCalls);
  TEST_ASSERT_EQUAL_INT(0, t.remountCalls);
}

// mkfs ok but the card will not re-mount afterward -> RemountFailed (an honest
// "formatted but not usable now" rather than a false success).
static void test_remount_failure_reported() {
  Trace t;
  FormatResult r = runFormat(
      /*mounted=*/true,
      [&] { t.mkfsCalls++; return true; },
      [&] { t.remountCalls++; return false; });
  TEST_ASSERT_EQUAL(FormatResult::RemountFailed, r);
  TEST_ASSERT_EQUAL_INT(1, t.mkfsCalls);
  TEST_ASSERT_EQUAL_INT(1, t.remountCalls);
}

// Happy path -> Ok, mkfs runs exactly once, remount runs exactly once, and mkfs
// strictly precedes remount.
static void test_happy_path_orders_mkfs_then_remount() {
  Trace t;
  bool mkfsDone = false;
  FormatResult r = runFormat(
      /*mounted=*/true,
      [&] { t.mkfsCalls++; mkfsDone = true; return true; },
      [&] { t.remountCalls++; if (!mkfsDone) t.mkfsBeforeRemount = false; return true; });
  TEST_ASSERT_EQUAL(FormatResult::Ok, r);
  TEST_ASSERT_EQUAL_INT(1, t.mkfsCalls);
  TEST_ASSERT_EQUAL_INT(1, t.remountCalls);
  TEST_ASSERT_TRUE(t.mkfsBeforeRemount);
}

// The machine-facing result tags are stable (logs + callers key off them).
static void test_result_strings_stable() {
  TEST_ASSERT_EQUAL_STRING("ok", formatResultStr(FormatResult::Ok));
  TEST_ASSERT_EQUAL_STRING("no-card", formatResultStr(FormatResult::NoCard));
  TEST_ASSERT_EQUAL_STRING("mkfs-failed", formatResultStr(FormatResult::MkfsFailed));
  TEST_ASSERT_EQUAL_STRING("remount-failed", formatResultStr(FormatResult::RemountFailed));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_no_card_refuses_without_touching);
  RUN_TEST(test_mkfs_failure_skips_remount);
  RUN_TEST(test_remount_failure_reported);
  RUN_TEST(test_happy_path_orders_mkfs_then_remount);
  RUN_TEST(test_result_strings_stable);
  return UNITY_END();
}
