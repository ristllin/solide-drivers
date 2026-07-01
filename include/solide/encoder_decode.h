#pragma once
#include <cstdint>

// Portable, host-testable input logic for the EC11 rotary encoder + push button.
// No Arduino dependencies — the hardware glue (ISR / digitalRead / millis) lives
// in src/hw/encoder.* and feeds these classes raw levels + a millis timestamp.
namespace solide::input {

// Quadrature decoder. Feed raw A/B levels each poll/edge; emits -1, 0, or +1 per
// completed detent. EC11 typically produces `stepsPerDetent` (4) quarter-steps
// between physical clicks.
class QuadDecoder {
 public:
  explicit QuadDecoder(int8_t stepsPerDetent = 4) : per_(stepsPerDetent) {}
  int8_t update(bool a, bool b);  // -1 / 0 / +1 (one unit per detent)
  void reset();

 private:
  uint8_t prev_ = 0;
  int16_t accum_ = 0;
  int8_t per_;
  bool seeded_ = false;
};

enum class BtnEvent : uint8_t { None, Click, LongPress };

// Debounced push button. Feed raw "pressed" + monotonic millis each poll.
// Emits Click on release of a short press, or LongPress once while held.
class Button {
 public:
  Button(uint32_t debounceMs = 8, uint32_t longMs = 600)
      : debounceMs_(debounceMs), longMs_(longMs) {}
  BtnEvent update(bool rawPressed, uint32_t nowMs);
  bool isPressed() const { return stable_; }  // debounced state (for hold-to-talk)
  void reset();

 private:
  uint32_t debounceMs_, longMs_;
  bool stable_ = false;
  bool raw_ = false;
  uint32_t lastChange_ = 0;
  uint32_t pressStart_ = 0;
  bool longFired_ = false;
  bool init_ = false;
};

}  // namespace solide::input
