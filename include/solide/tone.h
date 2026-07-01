#pragma once
#include <cstdint>
#include <cstddef>

// Portable, host-testable single-tone detection + level metering for the
// speaker->mic acoustic loopback self-test. No Arduino deps.
namespace solide::tone {

// Goertzel magnitude of `freq` Hz in a mono 16-bit buffer sampled at `rate`.
// Returned value is a per-sample-normalized amplitude estimate: large when the
// buffer contains energy at `freq`, near-zero otherwise. Compare to a threshold.
float goertzel(const int16_t* buf, size_t n, uint32_t rate, float freq);

// RMS level of a mono 16-bit buffer (0..~23170 for full-scale).
uint16_t rms(const int16_t* buf, size_t n);

// Peak absolute sample (0..32767).
uint16_t peak(const int16_t* buf, size_t n);

}  // namespace solide::tone
