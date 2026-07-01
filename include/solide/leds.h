#pragma once
#include <Arduino.h>
#include "solide/ring.h"     // ring:: — the portable status/layout/animation core

// WS2812B ring driver. A background task renders at ~60 FPS; the public API just
// sets state (non-blocking, safe to call from any task).
//
// Two layers:
//   * Single-ring PATTERNS (leds::show / off) — boot spinner, wifi rainbow, etc.
//   * Agent-status SEGMENTS (leds::agent*) — allocate ring arcs to sessions and
//     show each one's status via colour + animation + brightness. Backed by
//     ring:: (host-tested) and aligned with the nuage-solide-notify status model.
//
// S3 evolution of the classic driver (src/hw/leds.{h,cpp}). Render pacing is now
// LED_FRAME_MS (was 500 ms — a classic-ESP32 RMT-leak cap that the S3 doesn't
// need), which makes the rainbow and every animation smooth.
namespace solide::leds {

// Default global brightness cap (0-255) — USB-safe current on a 45-LED ring.
#ifndef LED_BRIGHTNESS
#define LED_BRIGHTNESS 30
#endif

// Render cadence. ~60 FPS: 45 LEDs push in ~1.65 ms and the S3 RMT path has no
// per-show heap leak, so this is comfortably safe (validated on hardware +
// heap-watched). Lower it (e.g. 33) to trade smoothness for CPU/power.
#ifndef LED_FRAME_MS
#define LED_FRAME_MS 16
#endif

// Dark LEDs left between adjacent segments so boundaries are clearly visible.
#ifndef LED_SEG_GAP
#define LED_SEG_GAP 1
#endif

// ---- Single-ring patterns ---------------------------------------------------
enum class Pattern : uint8_t {
  Off,
  Solid,
  Spinner,   // sweeping tail (processing / busy)
  Pulse,     // slow full-ring breathe (listening or error)
  Rainbow,   // smooth hue-cycling ring (wifi connecting / setup / idle)
  Flash,     // rapid on/off bursts (success affirmation)
};

bool begin();             // false if the render task can't start (heap)
bool taskAlive();         // true once the render task is running (self-test)
uint32_t stackHighWaterBytes();  // min free render-task stack seen (0 until started)

// MAX global brightness (0-255). Every pattern AND every agent segment is scaled
// to map within [0, max] — set it low for a dim ring, high for a bright one; the
// animations keep their full relative range within the new ceiling.
void    setBrightness(uint8_t maxB);
uint8_t maxBrightness();

// Ambient colour scheme for the single-ring cycle pattern (Rainbow = full wheel,
// or a curated palette). Does not change semantic agent-status colours.
void         setScheme(ring::Scheme s);
ring::Scheme scheme();

void show(Pattern p, uint8_t r = 0, uint8_t g = 120, uint8_t b = 255);
void off();

// ---- Agent-status segments (the strong abstraction) -------------------------
// Allocate/update a ring segment for a session and drive it by STATUS. `key` is
// any stable per-session id (e.g. a hash of the session string). The status maps
// to colour + animation via ring::styleFor(). ring::Status::Offline frees the
// segment. When >=1 segment is active it takes over the ring; agentClear()
// returns the ring to whatever single Pattern is set.
//
//   leds::agentStatus(sid, ring::Status::Running);          // blue comet ("thinking")
//   leds::agentAccent(sid, /*provider hue*/ 170);           // provider marker pixel
//   leds::agentStatus(sid, ring::Status::AwaitingApproval); // amber blink
//   leds::agentStatus(sid, ring::Status::Done);             // green fade -> ember
//   leds::agentStatus(sid, ring::Status::Offline);          // free the segment
bool agentStatus(uint32_t key, ring::Status st);   // false if the ring is full
void agentAccent(uint32_t key, uint8_t hue);        // provider accent hue (0-254; 255=white)
void agentProgress(uint32_t key, uint8_t pct);      // 0-100 (optional)
void agentClear();
int  agentCount();

// ---- debug / self-test snapshot --------------------------------------------
struct State {
  uint8_t pattern;       // current single-ring Pattern
  uint8_t r, g, b;       // active single-ring colour
  uint8_t bright;        // active brightness cap
  int     segCount;      // active agent segment count
  bool    taskAlive;     // render task running?
};
State currentState();

const char* patternName(Pattern p);   // off/solid/spinner/pulse/rainbow/flash

}  // namespace solide::leds
