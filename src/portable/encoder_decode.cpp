#include "solide/encoder_decode.h"

namespace solide::input {

// index = (prev << 2) | cur, where state = (A<<1)|B. Standard Gray-code table:
// +1 for one rotation direction, -1 for the other, 0 for no/invalid transition.
static const int8_t kQuadTable[16] = {
    0, -1, +1, 0,
    +1, 0, 0, -1,
    -1, 0, 0, +1,
    0, +1, -1, 0};

void QuadDecoder::reset() {
  prev_ = 0;
  accum_ = 0;
  seeded_ = false;
}

int8_t QuadDecoder::update(bool a, bool b) {
  uint8_t cur = static_cast<uint8_t>((a ? 2 : 0) | (b ? 1 : 0));
  if (!seeded_) {
    prev_ = cur;
    seeded_ = true;
    return 0;
  }
  int8_t d = kQuadTable[(prev_ << 2) | cur];
  prev_ = cur;
  if (d == 0) return 0;
  accum_ += d;
  if (per_ <= 1) {
    accum_ = 0;
    return d > 0 ? +1 : -1;
  }
  if (accum_ >= per_) { accum_ -= per_; return +1; }
  if (accum_ <= -per_) { accum_ += per_; return -1; }
  return 0;
}

void Button::reset() {
  stable_ = raw_ = longFired_ = init_ = false;
}

BtnEvent Button::update(bool rawPressed, uint32_t nowMs) {
  if (!init_) {
    init_ = true;
    raw_ = rawPressed;
    stable_ = false;  // assume released; a press held at power-on must still pass debounce
    lastChange_ = pressStart_ = nowMs;
    return BtnEvent::None;
  }
  if (rawPressed != raw_) {
    raw_ = rawPressed;
    lastChange_ = nowMs;
  }
  BtnEvent ev = BtnEvent::None;
  if (raw_ != stable_ && (nowMs - lastChange_) >= debounceMs_) {
    stable_ = raw_;
    if (stable_) {
      pressStart_ = nowMs;
      longFired_ = false;
    } else if (!longFired_ && (nowMs - pressStart_) < longMs_) {
      ev = BtnEvent::Click;
    }
  }
  if (stable_ && !longFired_ && (nowMs - pressStart_) >= longMs_) {
    longFired_ = true;
    ev = BtnEvent::LongPress;
  }
  return ev;
}

}  // namespace solide::input
