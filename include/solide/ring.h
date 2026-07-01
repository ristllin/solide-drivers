#pragma once
#include <cstdint>

// ============================================================================
// ring:: — the portable, host-testable core of the LED-ring status compositor.
//
// This is the ABSTRACTION for "allocate ring segments to agent sessions and show
// each session's status via colour + pattern + brightness". It is deliberately
// free of Arduino / NeoPixel dependencies so it can be unit-tested on the host
// (pio test -e native). The device driver (src/hw/leds.cpp) turns these pure
// decisions into pixels.
//
// The Status / Anim enums and the styleFor() table are byte-compatible with the
// sibling project nuage-solide-notify (host/notify/state.py) on purpose: the two
// share one vocabulary, and the S3 ring can later consume notify's wire frames
// directly. See docs and that project for the canonical model.
// ============================================================================

namespace solide::ring {

// ---- Agent/session status (== notify State) --------------------------------
enum class Status : uint8_t {
  Idle             = 0,  // session open, no active turn
  Running          = 1,  // model / tool working  ("thinking")
  WaitingInput     = 2,  // awaiting a human answer (HITL)
  AwaitingApproval = 3,  // tool / permission gate ("requires approval")
  Done             = 4,  // turn finished
  Error            = 5,  // errored
  Offline          = 6,  // session ended — frees the segment
};

// ---- Animation kinds (== notify Anim) --------------------------------------
enum class Anim : uint8_t {
  Off     = 0,
  Solid   = 1,
  Breathe = 2,   // slow brightness sine
  Comet   = 3,   // a bright head sweeping a fading tail (motion = "working")
  Blink   = 4,   // rapid on/off — grabs attention
  Fade     = 5,  // solid -> fade toward a dim ember (settled "done")
};

// Visual style for a status: base hue (0-254 on the colour wheel; 255 = white)
// plus the animation to run. Mirrors notify's STATE_STYLE.
struct Style { uint8_t hue; Anim anim; };

Style       styleFor(Status s);
uint8_t     priorityFor(Status s);   // higher == more important (never hide it)
const char* statusName(Status s);

// ---- Segment allocator -----------------------------------------------------
// Fixed-capacity. Lowest-free-index assignment, so insertion order == ring
// order; freed indices recycle immediately. When full, a higher-priority status
// evicts the lowest-priority occupant (so AwaitingApproval / WaitingInput / Error
// are never starved). Mirrors notify's SegmentAllocator + STATE_PRIORITY.
#ifndef RING_MAX_SEGMENTS
#define RING_MAX_SEGMENTS 8
#endif

struct Slot {
  bool     used      = false;
  uint32_t key       = 0;                 // caller's stable session key
  Status   status    = Status::Idle;
  uint32_t enteredAt = 0;                  // ms when status last CHANGED (drives Fade)
  bool     hasAccent = false;              // is a provider accent set? (all hue values are valid)
  uint8_t  accentHue = 0;                  // provider hue (0-254; 255=white) when hasAccent
  uint8_t  progress  = 0;                  // 0-100 (optional)
};

class Allocator {
 public:
  // Register-or-update a session. status==Offline frees the slot. Returns the
  // segment index (0..cap-1), or -1 if freed / the ring is full and the incoming
  // status cannot evict anyone. `nowMs` timestamps a status CHANGE.
  int  upsert(uint32_t key, Status st, uint32_t nowMs);
  void remove(uint32_t key);
  void clear();
  int  count() const;

  void setAccent(uint32_t key, uint8_t hue);
  void setProgress(uint32_t key, uint8_t pct);

  // Copy the used slots, ordered by segment index (== ring position), into out[].
  // Returns the number written.
  int  snapshot(Slot out[], int maxOut) const;

  // The most-important active status (for a single "overall" indicator). Returns
  // Offline if empty.
  Status highestPriority() const;

  int  capacity() const { return RING_MAX_SEGMENTS; }

 private:
  int  find(uint32_t key) const;
  int  nextFree() const;
  int  lowestPriorityUsed() const;
  Slot slots_[RING_MAX_SEGMENTS];   // index == ring position
};

// ---- Layout geometry -------------------------------------------------------
// Distribute `ledCount` LEDs among `segCount` segments around a RING, leaving
// `gap` dark LEDs between adjacent segments (including the last->first wrap when
// segCount>=2; a single segment fills the whole ring with no gap). The remainder
// is spread across the first segments so lengths differ by at most 1. If the gaps
// don't fit, `gap` is shrunk (down to 0) until they do. Returns segments written.
struct Span { int start; int len; };
int layout(int ledCount, int segCount, int gap, Span out[], int maxOut);

// ---- Animation envelopes (pure; time in ms) --------------------------------
uint8_t breatheLevel(uint32_t t, uint16_t periodMs);   // sine 0.15..1.0 -> ~38..255
bool    blinkOn(uint32_t t, uint16_t periodMs);        // 50% duty cycle
uint8_t fadeLevel(uint32_t elapsedMs, uint16_t durMs); // 255 -> 0 over durMs, then 0
int     cometHead(uint32_t t, int len, uint16_t stepMs); // head index in [0,len)
uint8_t cometFalloff(int distFromHead, int tail);      // 255>>dist within tail, else 0

// ---- Colour schemes (ambient "cycle" palettes) -----------------------------
// The single-ring cycle pattern can render the full rainbow or a curated palette
// for a nicer look. Schemes affect the AMBIENT cycle only — agent-status colours
// stay semantic. Palette schemes are 4 RGB stops interpolated cyclically; Rainbow
// / Pastel are HSV sweeps. Pure + host-tested; the caller applies gamma.
struct RGB { uint8_t r, g, b; };

enum class Scheme : uint8_t {
  Rainbow = 0,  // full-saturation HSV wheel
  Pastel,       // soft, desaturated wheel
  Ocean,        // deep blue -> teal -> cyan
  Sunset,       // red -> orange -> magenta -> violet
  Forest,       // greens + amber
  Cyber,        // cyan <-> magenta <-> violet
  Ember,        // fire: deep red -> orange -> gold
  Aurora,       // green -> blue -> violet
  COUNT
};

RGB         schemeColor(Scheme s, uint16_t phase);   // phase 0..65535, cyclic
RGB         hsv(uint16_t hue, uint8_t sat, uint8_t val);
const char* schemeName(Scheme s);

}  // namespace solide::ring
