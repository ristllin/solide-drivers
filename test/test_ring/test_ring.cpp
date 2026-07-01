// Host unit tests for the portable ring:: status/layout/animation core.
//   pio test -e native   (from s3/)
#include <unity.h>
#include "solide/ring.h"

using namespace solide::ring;

void setUp() {}
void tearDown() {}

// ---- status -> style / priority (byte-compat with nuage-solide-notify) ------

static void test_style_table() {
  TEST_ASSERT_EQUAL_UINT8(170, styleFor(Status::Running).hue);
  TEST_ASSERT_TRUE(styleFor(Status::Running).anim == Anim::Comet);
  TEST_ASSERT_EQUAL_UINT8(32, styleFor(Status::AwaitingApproval).hue);
  TEST_ASSERT_TRUE(styleFor(Status::AwaitingApproval).anim == Anim::Blink);
  TEST_ASSERT_EQUAL_UINT8(213, styleFor(Status::WaitingInput).hue);
  TEST_ASSERT_TRUE(styleFor(Status::WaitingInput).anim == Anim::Breathe);
  TEST_ASSERT_EQUAL_UINT8(85, styleFor(Status::Done).hue);
  TEST_ASSERT_TRUE(styleFor(Status::Done).anim == Anim::Fade);
  TEST_ASSERT_TRUE(styleFor(Status::Error).anim == Anim::Solid);
  TEST_ASSERT_TRUE(styleFor(Status::Offline).anim == Anim::Off);
  TEST_ASSERT_EQUAL_UINT8(255, styleFor(Status::Idle).hue);   // white
}

static void test_priority_order() {
  // AwaitingApproval > WaitingInput > Error > Running/Done > Idle/Offline
  TEST_ASSERT_TRUE(priorityFor(Status::AwaitingApproval) > priorityFor(Status::WaitingInput));
  TEST_ASSERT_TRUE(priorityFor(Status::WaitingInput)     > priorityFor(Status::Error));
  TEST_ASSERT_TRUE(priorityFor(Status::Error)            > priorityFor(Status::Running));
  TEST_ASSERT_TRUE(priorityFor(Status::Running)          > priorityFor(Status::Idle));
  TEST_ASSERT_EQUAL_UINT8(priorityFor(Status::Running), priorityFor(Status::Done));
}

// ---- allocator --------------------------------------------------------------

static void test_alloc_lowest_free_index() {
  Allocator a;
  TEST_ASSERT_EQUAL_INT(0, a.upsert(100, Status::Running, 0));
  TEST_ASSERT_EQUAL_INT(1, a.upsert(200, Status::Running, 0));
  TEST_ASSERT_EQUAL_INT(2, a.upsert(300, Status::Running, 0));
  TEST_ASSERT_EQUAL_INT(3, a.count());
}

static void test_alloc_idempotent_and_recycle() {
  Allocator a;
  a.upsert(100, Status::Running, 0);
  a.upsert(200, Status::Running, 0);
  // Re-upsert same key -> same index, no new slot.
  TEST_ASSERT_EQUAL_INT(0, a.upsert(100, Status::WaitingInput, 5));
  TEST_ASSERT_EQUAL_INT(2, a.count());
  // Free index 0, next new session recycles it.
  a.remove(100);
  TEST_ASSERT_EQUAL_INT(1, a.count());
  TEST_ASSERT_EQUAL_INT(0, a.upsert(300, Status::Running, 0));
}

static void test_alloc_offline_frees() {
  Allocator a;
  a.upsert(100, Status::Running, 0);
  TEST_ASSERT_EQUAL_INT(-1, a.upsert(100, Status::Offline, 0));  // Offline == free
  TEST_ASSERT_EQUAL_INT(0, a.count());
}

static void test_alloc_enteredAt_only_on_change() {
  Allocator a;
  a.upsert(100, Status::Running, 100);
  a.upsert(100, Status::Running, 200);   // same status -> keep original stamp
  Slot s[RING_MAX_SEGMENTS];
  a.snapshot(s, RING_MAX_SEGMENTS);
  TEST_ASSERT_EQUAL_UINT32(100, s[0].enteredAt);
  a.upsert(100, Status::Done, 300);      // changed -> restamp (Fade starts now)
  a.snapshot(s, RING_MAX_SEGMENTS);
  TEST_ASSERT_EQUAL_UINT32(300, s[0].enteredAt);
}

static void test_alloc_full_priority_eviction() {
  Allocator a;
  // Fill all slots with the lowest priority (Idle).
  for (uint32_t k = 0; k < RING_MAX_SEGMENTS; k++) a.upsert(1000 + k, Status::Idle, 0);
  TEST_ASSERT_EQUAL_INT(RING_MAX_SEGMENTS, a.count());
  // A higher-priority status evicts a lesser occupant and takes a slot.
  int idx = a.upsert(9999, Status::AwaitingApproval, 0);
  TEST_ASSERT_TRUE(idx >= 0);
  TEST_ASSERT_EQUAL_INT(RING_MAX_SEGMENTS, a.count());
  // Now everyone is at least priority... fill with AwaitingApproval, a new
  // equal/lower status must be refused.
  for (uint32_t k = 0; k < RING_MAX_SEGMENTS; k++) a.upsert(1000 + k, Status::AwaitingApproval, 0);
  TEST_ASSERT_EQUAL_INT(-1, a.upsert(7777, Status::Running, 0));
}

static void test_snapshot_ordered_and_highest() {
  Allocator a;
  a.upsert(100, Status::Running, 0);      // idx 0
  a.upsert(200, Status::AwaitingApproval, 0); // idx 1
  a.upsert(300, Status::Idle, 0);         // idx 2
  Slot s[RING_MAX_SEGMENTS];
  int n = a.snapshot(s, RING_MAX_SEGMENTS);
  TEST_ASSERT_EQUAL_INT(3, n);
  TEST_ASSERT_EQUAL_UINT32(100, s[0].key);   // ordered by index
  TEST_ASSERT_EQUAL_UINT32(200, s[1].key);
  TEST_ASSERT_EQUAL_UINT32(300, s[2].key);
  TEST_ASSERT_TRUE(a.highestPriority() == Status::AwaitingApproval);
}

static void test_accent_and_progress() {
  Allocator a;
  a.upsert(100, Status::Running, 0);
  a.setAccent(100, 170);
  a.setProgress(100, 250);   // clamps to 100
  Slot s[RING_MAX_SEGMENTS];
  a.snapshot(s, RING_MAX_SEGMENTS);
  TEST_ASSERT_TRUE(s[0].hasAccent);            // flag set (distinct from any hue value)
  TEST_ASSERT_EQUAL_UINT8(170, s[0].accentHue);
  TEST_ASSERT_EQUAL_UINT8(100, s[0].progress);
  // A fresh slot has no accent even at hue 0.
  a.upsert(200, Status::Running, 0);
  a.snapshot(s, RING_MAX_SEGMENTS);
  TEST_ASSERT_FALSE(s[1].hasAccent);
}

// ---- layout geometry --------------------------------------------------------

static int sumLens(const Span* s, int n) { int t = 0; for (int i = 0; i < n; i++) t += s[i].len; return t; }

static void test_layout_single_fills_ring() {
  Span s[RING_MAX_SEGMENTS];
  int n = layout(45, 1, 1, s, RING_MAX_SEGMENTS);
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL_INT(0, s[0].start);
  TEST_ASSERT_EQUAL_INT(45, s[0].len);   // no gap for a lone segment
}

static void test_layout_gaps_and_remainder() {
  Span s[RING_MAX_SEGMENTS];
  int n = layout(45, 2, 1, s, RING_MAX_SEGMENTS);
  TEST_ASSERT_EQUAL_INT(2, n);
  // 2 gaps of 1 (ring wrap) => content 43 => 22 + 21.
  TEST_ASSERT_EQUAL_INT(22, s[0].len);
  TEST_ASSERT_EQUAL_INT(21, s[1].len);
  TEST_ASSERT_EQUAL_INT(0, s[0].start);
  TEST_ASSERT_EQUAL_INT(23, s[1].start);            // 22 + 1 gap
  TEST_ASSERT_EQUAL_INT(43, sumLens(s, n));         // + 2 gap LEDs == 45
}

static void test_layout_three_even() {
  Span s[RING_MAX_SEGMENTS];
  int n = layout(45, 3, 1, s, RING_MAX_SEGMENTS);
  TEST_ASSERT_EQUAL_INT(3, n);
  for (int i = 0; i < 3; i++) TEST_ASSERT_EQUAL_INT(14, s[i].len);  // 45-3gap=42 /3
  TEST_ASSERT_EQUAL_INT(0,  s[0].start);
  TEST_ASSERT_EQUAL_INT(15, s[1].start);
  TEST_ASSERT_EQUAL_INT(30, s[2].start);
}

static void test_layout_gap_shrinks_to_fit() {
  Span s[RING_MAX_SEGMENTS];
  int n = layout(4, 3, 2, s, RING_MAX_SEGMENTS);  // gaps can't fit -> shrink to 0
  TEST_ASSERT_EQUAL_INT(3, n);
  TEST_ASSERT_EQUAL_INT(4, sumLens(s, n));        // every LED used, no gap
  for (int i = 0; i < n; i++) {
    TEST_ASSERT_TRUE(s[i].start >= 0 && s[i].start < 4);
    TEST_ASSERT_TRUE(s[i].len >= 1);
  }
}

static void test_layout_starts_in_range() {
  Span s[RING_MAX_SEGMENTS];
  for (int segs = 1; segs <= RING_MAX_SEGMENTS; segs++) {
    int n = layout(45, segs, 1, s, RING_MAX_SEGMENTS);
    for (int i = 0; i < n; i++) {
      TEST_ASSERT_TRUE(s[i].start >= 0 && s[i].start < 45);
      TEST_ASSERT_TRUE(s[i].len >= 0);
    }
  }
}

// Strong invariant: across every segment count, segments never overlap an LED
// and content + gaps exactly tile the ring.
static void test_layout_no_overlap_and_tiles() {
  for (int segs = 1; segs <= RING_MAX_SEGMENTS; segs++) {
    Span s[RING_MAX_SEGMENTS];
    int n = layout(45, segs, 1, s, RING_MAX_SEGMENTS);
    TEST_ASSERT_EQUAL_INT(segs, n);
    int cover[45] = {0};
    int lit = 0;
    for (int i = 0; i < n; i++) {
      TEST_ASSERT_TRUE(s[i].len >= 1);              // no zero-length arc (n<=8 on 45 LEDs)
      for (int k = 0; k < s[i].len; k++) { cover[(s[i].start + k) % 45]++; lit++; }
    }
    for (int i = 0; i < 45; i++) TEST_ASSERT_TRUE(cover[i] <= 1);   // no overlap
    int gaps = (n >= 2) ? n : 0;                    // 1-LED gap per boundary (ring)
    TEST_ASSERT_EQUAL_INT(45, lit + gaps);          // content + gaps tile the ring
  }
}

// Eviction must remove the LOWEST-priority occupant, never a more important one.
static void test_eviction_picks_lowest_priority() {
  Allocator a;
  a.upsert(1, Status::Running, 0);                  // pri 1, idx 0
  for (uint32_t k = 2; k <= RING_MAX_SEGMENTS; k++) a.upsert(k, Status::Idle, 0);  // pri 0
  TEST_ASSERT_EQUAL_INT(RING_MAX_SEGMENTS, a.count());
  int idx = a.upsert(999, Status::Error, 5);        // pri 2 evicts an Idle (pri 0)
  TEST_ASSERT_TRUE(idx >= 0);
  Slot s[RING_MAX_SEGMENTS];
  int n = a.snapshot(s, RING_MAX_SEGMENTS);
  bool runningAlive = false, errorPresent = false; int idleCount = 0;
  for (int i = 0; i < n; i++) {
    if (s[i].key == 1 && s[i].status == Status::Running) runningAlive = true;
    if (s[i].key == 999 && s[i].status == Status::Error) errorPresent = true;
    if (s[i].status == Status::Idle) idleCount++;
  }
  TEST_ASSERT_TRUE(runningAlive);                    // the Running was NOT evicted
  TEST_ASSERT_TRUE(errorPresent);
  TEST_ASSERT_EQUAL_INT(RING_MAX_SEGMENTS - 2, idleCount);   // exactly one Idle evicted
}

// Updating an existing session keeps its ring index; accent/progress survive a
// status change; enteredAt restamps only on the change.
static void test_update_keeps_index_and_metadata() {
  Allocator a;
  a.upsert(10, Status::Running, 0);
  a.upsert(20, Status::Running, 0);                 // key 20 -> idx 1
  a.setAccent(20, 90);
  a.setProgress(20, 40);
  TEST_ASSERT_EQUAL_INT(1, a.upsert(20, Status::Done, 100));   // same index on change
  Slot s[RING_MAX_SEGMENTS];
  int n = a.snapshot(s, RING_MAX_SEGMENTS);
  for (int i = 0; i < n; i++) if (s[i].key == 20) {
    TEST_ASSERT_TRUE(s[i].status == Status::Done);
    TEST_ASSERT_EQUAL_UINT8(90, s[i].accentHue);    // accent survived the status change
    TEST_ASSERT_EQUAL_UINT8(40, s[i].progress);     // progress survived
    TEST_ASSERT_EQUAL_UINT32(100, s[i].enteredAt);  // restamped on the change
  }
}

// snapshot() must not write past maxOut.
static void test_snapshot_maxout_clamp() {
  Allocator a;
  for (uint32_t k = 0; k < 4; k++) a.upsert(100 + k, Status::Running, 0);
  Slot s[8];
  for (int i = 0; i < 8; i++) s[i].key = 0xDEAD;     // canary
  int n = a.snapshot(s, 2);                          // ask for only 2
  TEST_ASSERT_EQUAL_INT(2, n);
  TEST_ASSERT_EQUAL_UINT32(0xDEAD, s[2].key);        // slot 2 untouched
}

// ---- animation envelopes ----------------------------------------------------

static void test_breathe_range() {
  TEST_ASSERT_UINT8_WITHIN(4, 38, breatheLevel(0, 1000));   // trough ~15% -> ~38
  TEST_ASSERT_EQUAL_UINT8(255, breatheLevel(500, 1000));    // crest -> full
}

static void test_blink_duty() {
  TEST_ASSERT_TRUE(blinkOn(0, 300));
  TEST_ASSERT_FALSE(blinkOn(150, 300));
  TEST_ASSERT_FALSE(blinkOn(299, 300));
  TEST_ASSERT_TRUE(blinkOn(300, 300));   // wraps
}

static void test_fade_monotonic() {
  TEST_ASSERT_EQUAL_UINT8(255, fadeLevel(0, 1500));
  TEST_ASSERT_EQUAL_UINT8(0, fadeLevel(1500, 1500));
  TEST_ASSERT_EQUAL_UINT8(0, fadeLevel(3000, 1500));   // clamps past end
  uint8_t a = fadeLevel(300, 1500), b = fadeLevel(900, 1500);
  TEST_ASSERT_TRUE(a > b);                              // strictly decreasing
}

static void test_comet() {
  TEST_ASSERT_EQUAL_INT(0, cometHead(0, 10, 60));
  TEST_ASSERT_EQUAL_INT(1, cometHead(60, 10, 60));
  TEST_ASSERT_EQUAL_INT(0, cometHead(600, 10, 60));    // 10 % 10
  TEST_ASSERT_EQUAL_UINT8(255, cometFalloff(0, 5));
  TEST_ASSERT_EQUAL_UINT8(127, cometFalloff(1, 5));
  TEST_ASSERT_EQUAL_UINT8(0, cometFalloff(5, 5));
  TEST_ASSERT_EQUAL_UINT8(0, cometFalloff(-1, 5));
}

// ---- colour schemes ---------------------------------------------------------

static int iabs(int x) { return x < 0 ? -x : x; }

static void test_hsv_primaries() {
  RGB r = hsv(0, 255, 255);
  TEST_ASSERT_TRUE(r.r > 240 && r.g < 20 && r.b < 20);        // red
  RGB g = hsv(21845, 255, 255);
  TEST_ASSERT_TRUE(g.g > 240 && g.r < 30 && g.b < 30);        // green
  RGB b = hsv(43690, 255, 255);
  TEST_ASSERT_TRUE(b.b > 240 && b.r < 30 && b.g < 30);        // blue
  RGB gray = hsv(12345, 0, 200);                              // sat 0 -> neutral gray
  TEST_ASSERT_EQUAL_UINT8(gray.r, gray.g);
  TEST_ASSERT_EQUAL_UINT8(gray.g, gray.b);
}

static void test_scheme_palette_stops() {
  RGB s0 = schemeColor(Scheme::Ocean, 0);                    // first stop
  TEST_ASSERT_EQUAL_UINT8(0, s0.r); TEST_ASSERT_EQUAL_UINT8(40, s0.g); TEST_ASSERT_EQUAL_UINT8(120, s0.b);
  RGB s1 = schemeColor(Scheme::Ocean, 16384);               // second stop (¼ phase)
  TEST_ASSERT_EQUAL_UINT8(0, s1.r); TEST_ASSERT_EQUAL_UINT8(140, s1.g); TEST_ASSERT_EQUAL_UINT8(150, s1.b);
}

static void test_scheme_cyclic_continuity() {
  // Every palette scheme must wrap smoothly: colour at phase 65535 ≈ phase 0.
  for (int si = (int)Scheme::Ocean; si < (int)Scheme::COUNT; si++) {
    RGB a = schemeColor((Scheme)si, 0);
    RGB b = schemeColor((Scheme)si, 65535);
    TEST_ASSERT_TRUE(iabs((int)a.r - (int)b.r) <= 3);
    TEST_ASSERT_TRUE(iabs((int)a.g - (int)b.g) <= 3);
    TEST_ASSERT_TRUE(iabs((int)a.b - (int)b.b) <= 3);
  }
}

static void test_scheme_names() {
  TEST_ASSERT_EQUAL_STRING("rainbow", schemeName(Scheme::Rainbow));
  TEST_ASSERT_EQUAL_STRING("aurora", schemeName(Scheme::Aurora));
  for (int si = 0; si < (int)Scheme::COUNT; si++)
    TEST_ASSERT_TRUE(schemeName((Scheme)si)[0] != '?');       // every scheme is named
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_style_table);
  RUN_TEST(test_priority_order);
  RUN_TEST(test_alloc_lowest_free_index);
  RUN_TEST(test_alloc_idempotent_and_recycle);
  RUN_TEST(test_alloc_offline_frees);
  RUN_TEST(test_alloc_enteredAt_only_on_change);
  RUN_TEST(test_alloc_full_priority_eviction);
  RUN_TEST(test_snapshot_ordered_and_highest);
  RUN_TEST(test_accent_and_progress);
  RUN_TEST(test_layout_single_fills_ring);
  RUN_TEST(test_layout_gaps_and_remainder);
  RUN_TEST(test_layout_three_even);
  RUN_TEST(test_layout_gap_shrinks_to_fit);
  RUN_TEST(test_layout_starts_in_range);
  RUN_TEST(test_layout_no_overlap_and_tiles);
  RUN_TEST(test_eviction_picks_lowest_priority);
  RUN_TEST(test_update_keeps_index_and_metadata);
  RUN_TEST(test_snapshot_maxout_clamp);
  RUN_TEST(test_breathe_range);
  RUN_TEST(test_blink_duty);
  RUN_TEST(test_fade_monotonic);
  RUN_TEST(test_comet);
  RUN_TEST(test_hsv_primaries);
  RUN_TEST(test_scheme_palette_stops);
  RUN_TEST(test_scheme_cyclic_continuity);
  RUN_TEST(test_scheme_names);
  return UNITY_END();
}
