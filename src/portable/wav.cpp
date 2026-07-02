#include "solide/wav.h"
#include <cstring>

namespace solide::wav {

static inline uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t rd32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline void wr16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; }
static inline void wr32(uint8_t* p, uint32_t v) {
  p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}

bool parseHeader(const uint8_t* b, size_t n, WavInfo& out) {
  if (!b || n < 44) return false;
  if (memcmp(b, "RIFF", 4) != 0 || memcmp(b + 8, "WAVE", 4) != 0) return false;

  WavInfo info{};
  bool haveFmt = false, haveData = false;
  size_t off = 12;                                   // first chunk after "WAVE"
  while (off + 8 <= n) {
    const uint8_t* ch = b + off;
    uint32_t sz = rd32(ch + 4);
    size_t body = off + 8;
    if (memcmp(ch, "fmt ", 4) == 0) {
      if (body + 16 > n) return false;               // truncated fmt
      uint16_t fmt = rd16(b + body);
      if (fmt != 1) return false;                    // PCM only
      info.channels      = rd16(b + body + 2);
      info.sampleRate    = rd32(b + body + 4);
      info.bitsPerSample = rd16(b + body + 14);
      haveFmt = true;
    } else if (memcmp(ch, "data", 4) == 0) {
      info.dataOffset = (uint32_t)body;
      info.dataBytes  = sz;
      haveData = true;
      break;                                         // data reached
    }
    // Advance in 64-bit so a file-controlled `sz` can't wrap `size_t` (32-bit on
    // the device) and loop forever; require forward progress + stay in-buffer.
    uint64_t next = (uint64_t)body + sz + (sz & 1);  // chunks are word-aligned
    if (next <= off || next > n) break;
    off = (size_t)next;
  }
  if (!haveFmt || !haveData) return false;
  out = info;
  return true;
}

void writeHeader(uint8_t o[44], uint32_t sampleRate, uint32_t dataBytes,
                 uint16_t channels, uint16_t bits) {
  uint32_t byteRate  = sampleRate * channels * (bits / 8);
  uint16_t blockAlign = (uint16_t)(channels * (bits / 8));
  memcpy(o + 0, "RIFF", 4);
  wr32(o + 4, 36 + dataBytes);                       // RIFF chunk size
  memcpy(o + 8, "WAVE", 4);
  memcpy(o + 12, "fmt ", 4);
  wr32(o + 16, 16);                                  // fmt chunk size (PCM)
  wr16(o + 20, 1);                                   // audioFormat = PCM
  wr16(o + 22, channels);
  wr32(o + 24, sampleRate);
  wr32(o + 28, byteRate);
  wr16(o + 32, blockAlign);
  wr16(o + 34, bits);
  memcpy(o + 36, "data", 4);
  wr32(o + 40, dataBytes);
}

}  // namespace solide::wav
