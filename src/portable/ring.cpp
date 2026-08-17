#include "solide/ring.h"
#include <cmath>

namespace solide::ring {

// ---- status -> visual + priority -------------------------------------------
// Values byte-compatible with nuage-solide-notify host/notify/state.py.

Style styleFor(Status s) {
  switch (s) {
    case Status::Idle:             return {255, Anim::Breathe};  // white breathe
    case Status::Running:          return {170, Anim::Comet};    // blue comet
    case Status::WaitingInput:     return {213, Anim::Breathe};  // purple breathe
    case Status::AwaitingApproval: return { 32, Anim::Blink};    // amber blink
    case Status::Done:             return { 85, Anim::Fade};     // green fade
    case Status::Error:            return {  0, Anim::Solid};    // red solid
    case Status::Offline:          return {  0, Anim::Off};      // off
  }
  return {0, Anim::Off};
}

uint8_t priorityFor(Status s) {
  switch (s) {
    case Status::AwaitingApproval: return 4;   // must never be hidden
    case Status::WaitingInput:     return 3;   // HITL
    case Status::Error:            return 2;   // important
    case Status::Running:          return 1;
    case Status::Done:             return 1;
    case Status::Idle:             return 0;
    case Status::Offline:          return 0;
  }
  return 0;
}

const char* statusName(Status s) {
  switch (s) {
    case Status::Idle:             return "idle";
    case Status::Running:          return "running";
    case Status::WaitingInput:     return "waiting-input";
    case Status::AwaitingApproval: return "awaiting-approval";
    case Status::Done:             return "done";
    case Status::Error:            return "error";
    case Status::Offline:          return "offline";
  }
  return "unknown";
}

// ---- allocator -------------------------------------------------------------

int Allocator::find(uint32_t key) const {
  for (int i = 0; i < RING_MAX_SEGMENTS; i++)
    if (slots_[i].used && slots_[i].key == key) return i;
  return -1;
}

int Allocator::nextFree() const {
  for (int i = 0; i < RING_MAX_SEGMENTS; i++)
    if (!slots_[i].used) return i;
  return -1;
}

int Allocator::lowestPriorityUsed() const {
  int best = -1;
  uint8_t bestPri = 255;
  for (int i = 0; i < RING_MAX_SEGMENTS; i++) {
    if (!slots_[i].used) continue;
    uint8_t p = priorityFor(slots_[i].status);
    if (best < 0 || p < bestPri) { best = i; bestPri = p; }
  }
  return best;
}

int Allocator::upsert(uint32_t key, Status st, uint32_t nowMs) {
  if (st == Status::Offline) { remove(key); return -1; }

  int i = find(key);
  if (i >= 0) {                       // existing session: update in place
    if (slots_[i].status != st) {
      slots_[i].status    = st;
      slots_[i].enteredAt = nowMs;    // stamp only on a real change (Fade/Breathe)
    }
    return i;
  }

  i = nextFree();
  if (i < 0) {                        // ring full: try to evict a lesser status
    int victim = lowestPriorityUsed();
    if (victim < 0 || priorityFor(st) <= priorityFor(slots_[victim].status))
      return -1;                      // nobody less important - refuse
    i = victim;
  }

  slots_[i].used      = true;
  slots_[i].key       = key;
  slots_[i].status    = st;
  slots_[i].enteredAt = nowMs;
  slots_[i].hasAccent = false;
  slots_[i].accentHue = 0;
  slots_[i].progress  = 0;
  return i;
}

void Allocator::remove(uint32_t key) {
  int i = find(key);
  if (i >= 0) slots_[i] = Slot{};
}

void Allocator::clear() {
  for (int i = 0; i < RING_MAX_SEGMENTS; i++) slots_[i] = Slot{};
}

int Allocator::count() const {
  int n = 0;
  for (int i = 0; i < RING_MAX_SEGMENTS; i++) if (slots_[i].used) n++;
  return n;
}

void Allocator::setAccent(uint32_t key, uint8_t hue) {
  int i = find(key);
  if (i >= 0) { slots_[i].accentHue = hue; slots_[i].hasAccent = true; }
}

void Allocator::setProgress(uint32_t key, uint8_t pct) {
  int i = find(key);
  if (i >= 0) slots_[i].progress = pct > 100 ? 100 : pct;
}

int Allocator::snapshot(Slot out[], int maxOut) const {
  int n = 0;
  for (int i = 0; i < RING_MAX_SEGMENTS && n < maxOut; i++)
    if (slots_[i].used) out[n++] = slots_[i];
  return n;
}

Status Allocator::highestPriority() const {
  int best = -1;
  uint8_t bestPri = 0;
  for (int i = 0; i < RING_MAX_SEGMENTS; i++) {
    if (!slots_[i].used) continue;
    uint8_t p = priorityFor(slots_[i].status);
    if (best < 0 || p > bestPri) { best = i; bestPri = p; }
  }
  return best < 0 ? Status::Offline : slots_[best].status;
}

// ---- layout ----------------------------------------------------------------

int layout(int ledCount, int segCount, int gap, Span out[], int maxOut) {
  if (segCount <= 0 || maxOut <= 0 || ledCount <= 0) return 0;
  int n = segCount < maxOut ? segCount : maxOut;

  // n segments on a ring => n boundaries => n gaps (incl. last->first wrap). A lone
  // segment ALSO gets a gap now (was: filled the whole ring), so one session reads
  // as an ARC, not a full circle - the caller passes a wider gap for n==1 to make
  // the arc unmistakable (owner: "full circle makes no sense").
  int gapCount = n;
  int g = gap < 0 ? 0 : gap;
  // Shrink the gap until segments get at least 1 LED each.
  while (g > 0 && (n + g * gapCount) > ledCount) g--;

  int gapTotal = g * gapCount;
  int content  = ledCount - gapTotal;
  if (content < 0) content = 0;
  int base = content / n;
  int rem  = content % n;   // first `rem` segments get one extra LED

  int start = 0;
  for (int i = 0; i < n; i++) {
    int len = base + (i < rem ? 1 : 0);
    out[i].start = start % ledCount;
    out[i].len   = len;
    start += len + g;       // advance past this segment + its trailing gap
  }
  return n;
}

// ---- animation envelopes ---------------------------------------------------

uint8_t breatheLevel(uint32_t t, uint16_t periodMs) {
  if (periodMs == 0) return 255;
  float ph = (float)(t % periodMs) / (float)periodMs;
  float s  = 0.15f + 0.85f * 0.5f * (1.0f - cosf(ph * 2.0f * (float)M_PI));
  int v = (int)(255.0f * s + 0.5f);
  return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

bool blinkOn(uint32_t t, uint16_t periodMs) {
  if (periodMs == 0) return true;
  return (t % periodMs) < (uint32_t)(periodMs / 2);
}

uint8_t fadeLevel(uint32_t elapsedMs, uint16_t durMs) {
  if (durMs == 0) return 0;
  if (elapsedMs >= durMs) return 0;
  int v = (int)(255.0f * (1.0f - (float)elapsedMs / (float)durMs) + 0.5f);
  return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

int cometHead(uint32_t t, int len, uint16_t stepMs) {
  if (len <= 0) return 0;
  if (stepMs == 0) stepMs = 1;
  return (int)((t / stepMs) % (uint32_t)len);
}

uint8_t cometFalloff(int distFromHead, int tail) {
  if (distFromHead < 0 || distFromHead >= tail || distFromHead >= 31) return 0;  // shift-safe
  return (uint8_t)(255u >> distFromHead);   // 255,127,63,31,... within the tail
}

// ---- colour schemes --------------------------------------------------------

RGB hsv(uint16_t hue, uint8_t sat, uint8_t val) {
  uint8_t h = hue >> 8;                    // 0-255
  uint8_t region = h / 43;                 // 0-5
  uint8_t rem = (uint8_t)((h - region * 43) * 6);
  // Divide by 255 (not >>8) so sat==0 yields an exact neutral (val on every ch).
  uint8_t p = (uint8_t)(val * (255 - sat) / 255);
  uint8_t q = (uint8_t)(val * (255 - sat * rem / 255) / 255);
  uint8_t t = (uint8_t)(val * (255 - sat * (255 - rem) / 255) / 255);
  switch (region) {
    case 0:  return {val, t, p};
    case 1:  return {q, val, p};
    case 2:  return {p, val, t};
    case 3:  return {p, q, val};
    case 4:  return {t, p, val};
    default: return {val, p, q};
  }
}

// 4 RGB stops per palette scheme (Ocean..Aurora), interpolated cyclically.
static const RGB PALETTES[][4] = {
  /* Ocean  */ {{  0, 40,120}, {  0,140,150}, {  0,210,190}, {  0, 90,150}},
  /* Sunset */ {{255, 70,  0}, {255,150,  0}, {220, 20, 90}, {110,  0,130}},
  /* Forest */ {{ 10, 95, 15}, { 95,165,  0}, {190,150,  0}, {  0,110, 60}},
  /* Cyber  */ {{  0,225,220}, {190,  0,225}, { 70,  0,210}, {  0,205,130}},
  /* Ember  */ {{ 90,  0,  0}, {230, 50,  0}, {255,150, 10}, {130, 15,  0}},
  /* Aurora */ {{  0,210,120}, {  0,130,210}, {130,  0,210}, {  0,205,175}},
};

static inline uint8_t lerp8(uint8_t a, uint8_t b, uint16_t frac) {
  return (uint8_t)((int)a + (((int)b - (int)a) * (int)frac) / 65536);
}

RGB schemeColor(Scheme s, uint16_t phase) {
  if (s == Scheme::Rainbow) return hsv(phase, 255, 255);
  if (s == Scheme::Pastel)  return hsv(phase, 110, 255);
  int pi = (int)s - (int)Scheme::Ocean;
  int nPal = (int)(sizeof(PALETTES) / sizeof(PALETTES[0]));
  if (pi < 0 || pi >= nPal) return hsv(phase, 255, 255);
  const RGB* p = PALETTES[pi];
  uint32_t seg = (uint32_t)phase * 4;        // 4 stops across the phase
  int      i    = (int)(seg >> 16);          // 0..3
  uint16_t frac = (uint16_t)(seg & 0xFFFF);  // 0..65535 between stop i and i+1
  const RGB& a = p[i & 3];
  const RGB& b = p[(i + 1) & 3];
  return {lerp8(a.r, b.r, frac), lerp8(a.g, b.g, frac), lerp8(a.b, b.b, frac)};
}

const char* schemeName(Scheme s) {
  switch (s) {
    case Scheme::Rainbow: return "rainbow";
    case Scheme::Pastel:  return "pastel";
    case Scheme::Ocean:   return "ocean";
    case Scheme::Sunset:  return "sunset";
    case Scheme::Forest:  return "forest";
    case Scheme::Cyber:   return "cyber";
    case Scheme::Ember:   return "ember";
    case Scheme::Aurora:  return "aurora";
    case Scheme::COUNT:   return "?";
  }
  return "?";
}

}  // namespace solide::ring
