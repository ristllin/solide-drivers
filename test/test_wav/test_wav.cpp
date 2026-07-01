// Host unit tests for solide::wav header parse/build.
#include <unity.h>
#include "solide/wav.h"
#include <cstring>

using namespace solide::wav;

void setUp() {}
void tearDown() {}

// Append a RIFF chunk (id + LE size + payload, word-aligned) at *o.
static void put(uint8_t* b, size_t& o, const char* id, const uint8_t* p, uint32_t sz) {
  memcpy(b + o, id, 4); o += 4;
  b[o] = sz & 0xFF; b[o+1] = (sz>>8)&0xFF; b[o+2] = (sz>>16)&0xFF; b[o+3] = (sz>>24)&0xFF; o += 4;
  for (uint32_t i = 0; i < sz; i++) b[o+i] = p ? p[i] : 0;
  o += sz;
  if (sz & 1) { b[o] = 0; o++; }
}

static void test_wav_roundtrip() {
  uint8_t h[44];
  writeHeader(h, 16000, 32000, 1, 16);          // 1 s of 16 kHz mono 16-bit
  WavInfo info;
  TEST_ASSERT_TRUE(parseHeader(h, 44, info));
  TEST_ASSERT_EQUAL_UINT32(16000, info.sampleRate);
  TEST_ASSERT_EQUAL_UINT16(1, info.channels);
  TEST_ASSERT_EQUAL_UINT16(16, info.bitsPerSample);
  TEST_ASSERT_EQUAL_UINT32(44, info.dataOffset);
  TEST_ASSERT_EQUAL_UINT32(32000, info.dataBytes);
}

static void test_wav_stereo_reported() {
  uint8_t h[44];
  writeHeader(h, 44100, 100, 2, 16);
  WavInfo info;
  TEST_ASSERT_TRUE(parseHeader(h, 44, info));
  TEST_ASSERT_EQUAL_UINT16(2, info.channels);   // reported; the caller judges
  TEST_ASSERT_EQUAL_UINT32(44100, info.sampleRate);
}

static void test_wav_rejects_non_riff() {
  uint8_t bad[44]; memset(bad, 0, 44); memcpy(bad, "XXXX", 4);
  WavInfo info;
  TEST_ASSERT_FALSE(parseHeader(bad, 44, info));
}

static void test_wav_rejects_truncated() {
  uint8_t h[44]; writeHeader(h, 16000, 100, 1, 16);
  WavInfo info;
  TEST_ASSERT_FALSE(parseHeader(h, 20, info));   // shorter than a header
}

static void test_wav_walks_extra_chunk() {
  uint8_t b[128]; memset(b, 0, sizeof(b));
  size_t o = 0;
  memcpy(b, "RIFF", 4); o = 4; o += 4; memcpy(b + 8, "WAVE", 4); o = 12;
  uint8_t fmt[16] = {0};
  fmt[0] = 1;                                    // PCM
  fmt[2] = 1;                                    // channels = 1
  fmt[4] = 0x80; fmt[5] = 0x3E;                  // sampleRate = 16000
  fmt[14] = 16;                                  // bits = 16
  put(b, o, "fmt ", fmt, 16);
  put(b, o, "LIST", (const uint8_t*)"INFO", 4);  // extra chunk before data
  uint8_t pcm[8] = {1,2,3,4,5,6,7,8};
  size_t dataHdr = o;
  put(b, o, "data", pcm, 8);
  WavInfo info;
  TEST_ASSERT_TRUE(parseHeader(b, o, info));
  TEST_ASSERT_EQUAL_UINT32(16000, info.sampleRate);
  TEST_ASSERT_EQUAL_UINT32((uint32_t)(dataHdr + 8), info.dataOffset);
  TEST_ASSERT_EQUAL_UINT32(8, info.dataBytes);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_wav_roundtrip);
  RUN_TEST(test_wav_stereo_reported);
  RUN_TEST(test_wav_rejects_non_riff);
  RUN_TEST(test_wav_rejects_truncated);
  RUN_TEST(test_wav_walks_extra_chunk);
  return UNITY_END();
}
