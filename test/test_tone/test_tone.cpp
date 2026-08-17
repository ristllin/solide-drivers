// Host unit tests for solide::tone (Goertzel + RMS/peak) - the loopback DSP.
#include <unity.h>
#include "solide/tone.h"
#include <cmath>
#include <cstring>

using namespace solide::tone;

void setUp() {}
void tearDown() {}

static void fillSine(int16_t* b, size_t n, uint32_t rate, float freq, int16_t amp) {
  for (size_t i = 0; i < n; i++)
    b[i] = (int16_t)(amp * sinf(2.0f * (float)M_PI * freq * (float)i / (float)rate));
}

static void test_tone_detects_target() {
  static int16_t b[800];
  fillSine(b, 800, 16000, 1000, 8000);
  float on  = goertzel(b, 800, 16000, 1000);       // energy present
  float off = goertzel(b, 800, 16000, 3500);       // no energy here
  TEST_ASSERT_TRUE(on > off * 5.0f);               // clearly distinguishable
  TEST_ASSERT_TRUE(on > 1000.0f);                  // absolute presence (~amp/2)
}

static void test_tone_silence() {
  static int16_t z[800]; memset(z, 0, sizeof(z));
  TEST_ASSERT_TRUE(goertzel(z, 800, 16000, 1000) < 1.0f);
  TEST_ASSERT_EQUAL_UINT16(0, rms(z, 800));
  TEST_ASSERT_EQUAL_UINT16(0, peak(z, 800));
}

static void test_tone_rms_peak() {
  static int16_t b[800];
  fillSine(b, 800, 16000, 1000, 8000);
  TEST_ASSERT_UINT16_WITHIN(500, 5657, rms(b, 800));   // sine RMS ~ amp/sqrt2
  TEST_ASSERT_UINT16_WITHIN(200, 8000, peak(b, 800));
}

static void test_tone_guards() {
  int16_t x[4] = {1, 2, 3, 4};
  TEST_ASSERT_EQUAL_FLOAT(0.0f, goertzel(nullptr, 4, 16000, 1000));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, goertzel(x, 0, 16000, 1000));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, goertzel(x, 4, 0, 1000));
  TEST_ASSERT_EQUAL_UINT16(0, rms(nullptr, 4));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_tone_detects_target);
  RUN_TEST(test_tone_silence);
  RUN_TEST(test_tone_rms_peak);
  RUN_TEST(test_tone_guards);
  return UNITY_END();
}
