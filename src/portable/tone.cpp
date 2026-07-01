#include "solide/tone.h"
#include <cmath>

namespace solide::tone {

float goertzel(const int16_t* x, size_t n, uint32_t rate, float freq) {
  if (!x || n == 0 || rate == 0) return 0.0f;
  float w     = 2.0f * (float)M_PI * freq / (float)rate;
  float coeff = 2.0f * cosf(w);
  float s1 = 0.0f, s2 = 0.0f;
  for (size_t i = 0; i < n; i++) {
    float s0 = (float)x[i] + coeff * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  float power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
  if (power < 0.0f) power = 0.0f;
  return sqrtf(power) / (float)n;   // per-sample-normalized amplitude estimate
}

uint16_t rms(const int16_t* x, size_t n) {
  if (!x || n == 0) return 0;
  double acc = 0.0;
  for (size_t i = 0; i < n; i++) { double v = x[i]; acc += v * v; }
  return (uint16_t)sqrt(acc / (double)n);
}

uint16_t peak(const int16_t* x, size_t n) {
  if (!x || n == 0) return 0;
  int p = 0;
  for (size_t i = 0; i < n; i++) {
    int a = x[i] < 0 ? -x[i] : x[i];
    if (a > p) p = a;
  }
  return (uint16_t)(p > 32767 ? 32767 : p);
}

}  // namespace solide::tone
