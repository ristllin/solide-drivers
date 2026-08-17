#pragma once
#include <Arduino.h>
#include "solide/ring.h"     // ring:: - the portable status/layout/animation core

// WS2812B ring driver. A background task renders at ~60 FPS; the public API just
// sets state (non-blocking, safe to call from any task).
//
// Three layers, highest precedence first:
//   * Raw FRAME (leds::showFrame / clearFrame) - a caller-composed RGB[] pushed
//     verbatim, once per call. For a caller with its own animation engine (e.g.
//     Nimbus's host-tested nimbus::ring::Animator) that wants full per-pixel
//     control. Takes over the ring the instant showFrame() is first called and
//     holds it until clearFrame() (or the frame goes stale - see showFrame()'s
//     doc comment) hands control back to whichever of the layers below is set.
//   * Agent-status SEGMENTS (leds::agent*) - allocate ring arcs to sessions and
//     show each one's status via colour + animation + brightness. Backed by
//     ring:: (host-tested) and aligned with the nuage-solide-notify status model.
//   * Single-ring PATTERNS (leds::show / off) - boot spinner, wifi rainbow, etc.
//     The fallback layer when there are no agent segments and no raw frame.
//
// S3 evolution of the classic driver (src/hw/leds.{h,cpp}). Render pacing is now
// LED_FRAME_MS (was 500 ms - a classic-ESP32 RMT-leak cap that the S3 doesn't
// need), which makes the rainbow and every animation smooth.
namespace solide::leds {

// Default global brightness cap (0-255) - USB-safe current on a 45-LED ring.
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

// Raw-frame staleness watchdog (see showFrame()'s doc comment, design point c):
// if showFrame() isn't called again within this window, raw-frame mode is
// auto-released back to the agent-segment/Pattern layers.
#ifndef LED_FRAME_STALE_MS
#define LED_FRAME_STALE_MS 500
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
// to map within [0, max] - set it low for a dim ring, high for a bright one; the
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

// ---- raw frame (the escape hatch for an external animation engine) ---------
// Push one already-composed RGB frame. Copies (never aliases) up to
// min(count, LED_COUNT) pixels into an internal buffer (guarded by the same
// spinlock as the rest of this module's cross-task state) that the render
// task reads on its next tick - non-blocking and safe from any task,
// mirroring every other setter here.
//
// Design decisions (see the header comment "Three layers" above for how this
// interacts with Pattern/agent-segment mode):
//  (a) Precedence: showFrame() is the highest-priority layer. The FIRST call
//      immediately takes over the ring, ahead of any active agent segments or
//      Pattern - it does not require the caller to first clear those. Calling
//      show()/agentStatus()/etc. does NOT implicitly exit raw-frame mode: they
//      just update the state those layers will show once raw-frame mode ends.
//      This is deliberate - a caller mid-animation (e.g. Nimbus's Animator
//      driving a boot/connect sequence) should not have its frames silently
//      interrupted by an unrelated agentStatus() call elsewhere in the system.
//  (b) Release: explicit only, via clearFrame() - mirrors off() for Pattern.
//      There is no implicit "showFrame(nullptr, 0)" release; count==0 is
//      instead defined as (d) below (an all-off frame), not a mode exit, so
//      the two concerns (what pixels show vs. which layer is active) stay
//      orthogonal.
//  (c) Staleness: showFrame() is meant to be called every render tick by a
//      live caller (Nimbus's glue calls Animator::frame() + showFrame() at
//      its own FPS). If the calling task stops calling - crash, deadlock,
//      logic bug - the ring must not freeze on a stale frame forever (the
//      "never hang silently" rule elsewhere in this ecosystem, e.g. the
//      serial TX timeout). So: if showFrame() has not been called again
//      within LED_FRAME_STALE_MS (default 500 ms, ~30 render frames - well
//      above any reasonable caller cadence, short enough a hang is not
//      visibly "stuck"), the render task automatically exits raw-frame mode
//      and falls back to the agent-segment/Pattern layers, same as an
//      explicit clearFrame(). A live caller pushing frames at any normal rate
//      never hits this; it only fires on an actual stall.
//  (d) Size mismatch: count is clamped, never rejected.
//        count < LED_COUNT: the given pixels are copied to indices
//          [0, count), the remaining tail [count, LED_COUNT) is left OFF
//          (black) rather than showing stale data from a previous frame.
//        count > LED_COUNT: only the first LED_COUNT pixels are copied; the
//          rest are silently ignored. count == 0 is valid and produces an
//          all-off frame (distinct from clearFrame(), which additionally
//          releases raw-frame mode back to the layers below).
//      Neither case is an error - a caller built against a different board's
//      LED_COUNT must not crash this one; solide::kBoardSolideS3.led.count is
//      the source of truth for the caller to size against.
void showFrame(const ring::RGB* pixels, size_t count);

// Explicitly release raw-frame mode back to agent-segment/Pattern rendering.
// Safe to call even if showFrame() was never called (a no-op then).
void clearFrame();

// ---- debug / self-test snapshot --------------------------------------------
struct State {
  uint8_t pattern;       // current single-ring Pattern
  uint8_t r, g, b;       // active single-ring colour
  uint8_t bright;        // active brightness cap
  int     segCount;      // active agent segment count
  bool    taskAlive;     // render task running?
  bool    rawFrame;      // true while showFrame() has control of the ring
};
State currentState();

const char* patternName(Pattern p);   // off/solid/spinner/pulse/rainbow/flash

}  // namespace solide::leds
