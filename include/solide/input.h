#pragma once
#include <cstdint>

// EC11 encoder + button. A poll task runs the portable core decoder/debounce
// and pushes events to a queue; drain with pop() from the main loop.
//
// S3 port of src/hw/encoder.{h,cpp} — pins from board.h; the debounce/decode is
// the host-tested core (s3/lib/core/encoder_decode). No config.h/logbuf dep.
#ifndef BTN_LONGPRESS_MS
// Long-press threshold (ms). A Click fires only on RELEASE before this window;
// hold longer and it's a LongPress (which closes / backs out of the menu). At
// 400 ms a firm, deliberate "press to select" easily overshot into a long-press,
// so descending into menu options felt unreliable. 600 ms gives a comfortable
// click window while still opening the menu / hold-to-talk on a real long hold.
#define BTN_LONGPRESS_MS 600
#endif

namespace solide::input {
enum class Event : uint8_t { None, RotateCW, RotateCCW, Click, LongPress };
bool begin();        // false if the queue or poll task can't be created
bool pop(Event& e);  // non-blocking
bool pressed();      // live (debounced) push-button state — for hold-to-talk release
bool taskAlive();    // true once the poll task is running (self-test)
}
