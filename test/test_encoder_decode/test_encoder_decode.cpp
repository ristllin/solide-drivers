// Host unit tests for solide::input QuadDecoder + Button (pure logic).
#include <unity.h>
#include "solide/encoder_decode.h"

using namespace solide::input;

void setUp() {}
void tearDown() {}

// One CW detent = 4 quarter-steps through the Gray code: state 0->2->3->1->0
// (A,B): (0,0)->(1,0)->(1,1)->(0,1)->(0,0). One CCW detent is the reverse.
static int cwDetent(QuadDecoder& d) {
  int s = 0;
  s += d.update(1, 0); s += d.update(1, 1); s += d.update(0, 1); s += d.update(0, 0);
  return s;
}
static int ccwDetent(QuadDecoder& d) {
  int s = 0;
  s += d.update(0, 1); s += d.update(1, 1); s += d.update(1, 0); s += d.update(0, 0);
  return s;
}

static void test_quad_cw() {
  QuadDecoder d(4);
  d.update(0, 0);                              // seed
  TEST_ASSERT_EQUAL_INT(1, cwDetent(d));       // one detent -> +1
  TEST_ASSERT_EQUAL_INT(1, cwDetent(d));       // repeatable
}

static void test_quad_ccw() {
  QuadDecoder d(4);
  d.update(0, 0);
  TEST_ASSERT_EQUAL_INT(-1, ccwDetent(d));
}

static void test_quad_no_output_mid_detent() {
  QuadDecoder d(4);
  d.update(0, 0);
  TEST_ASSERT_EQUAL_INT(0, d.update(1, 0));    // 1/4
  TEST_ASSERT_EQUAL_INT(0, d.update(1, 1));    // 2/4
  TEST_ASSERT_EQUAL_INT(0, d.update(0, 1));    // 3/4
  TEST_ASSERT_EQUAL_INT(1, d.update(0, 0));    // 4/4 -> detent
}

static void test_quad_per1_immediate() {
  QuadDecoder d(1);                            // 1 step per detent
  d.update(0, 0);
  TEST_ASSERT_EQUAL_INT(1, d.update(1, 0));    // immediate +1
}

static void test_button_click() {
  Button b(8, 600);
  b.update(false, 0);                                          // init released
  TEST_ASSERT_TRUE(b.update(true, 100) == BtnEvent::None);     // press (not yet debounced)
  TEST_ASSERT_TRUE(b.update(true, 110) == BtnEvent::None);     // debounced press, no event
  TEST_ASSERT_TRUE(b.isPressed());
  TEST_ASSERT_TRUE(b.update(false, 200) == BtnEvent::None);    // release edge
  TEST_ASSERT_TRUE(b.update(false, 210) == BtnEvent::Click);   // debounced release -> Click
  TEST_ASSERT_FALSE(b.isPressed());
}

static void test_button_longpress() {
  Button b(8, 600);
  b.update(false, 0);
  b.update(true, 100);
  b.update(true, 110);                                         // debounced press @110
  TEST_ASSERT_TRUE(b.update(true, 720) == BtnEvent::LongPress);// held >=600 -> once
  TEST_ASSERT_TRUE(b.update(true, 900) == BtnEvent::None);     // does not re-fire
  // release after a long-press must NOT also emit Click
  TEST_ASSERT_TRUE(b.update(false, 910) == BtnEvent::None);
  TEST_ASSERT_TRUE(b.update(false, 920) == BtnEvent::None);
}

static void test_button_debounce_rejects_bounce() {
  Button b(8, 600);
  b.update(false, 0);
  b.update(true, 100);                                         // raw press
  b.update(false, 103);                                        // bounced back within 8 ms
  TEST_ASSERT_TRUE(b.update(false, 120) == BtnEvent::None);    // never stabilized -> no click
  TEST_ASSERT_FALSE(b.isPressed());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_quad_cw);
  RUN_TEST(test_quad_ccw);
  RUN_TEST(test_quad_no_output_mid_detent);
  RUN_TEST(test_quad_per1_immediate);
  RUN_TEST(test_button_click);
  RUN_TEST(test_button_longpress);
  RUN_TEST(test_button_debounce_rejects_bounce);
  return UNITY_END();
}
