#pragma once
#include <cstdint>

// EC11 encoder + button. A poll task runs the portable core decoder/debounce
// and pushes events to a queue; drain with pop() from the main loop.
//
// S3 port of src/hw/encoder.{h,cpp} — pins from board.h; the debounce/decode is
// the host-tested core (s3/lib/core/encoder_decode). No config.h/logbuf dep.
#ifndef BTN_LONGPRESS_MS
#define BTN_LONGPRESS_MS 400   // long-press threshold (ms); classic config.h default
#endif

namespace solide::input {
enum class Event : uint8_t { None, RotateCW, RotateCCW, Click, LongPress };
bool begin();        // false if the queue or poll task can't be created
bool pop(Event& e);  // non-blocking
bool pressed();      // live (debounced) push-button state — for hold-to-talk release
bool taskAlive();    // true once the poll task is running (self-test)
}
