#include "solide/leds.h"
#include <Adafruit_NeoPixel.h>
#include "esp_heap_caps.h"
#include "solide/boards/board_solide_s3.h"

// LED pin + count from the canonical board config, as compile-time constants so
// the NeoPixel buffer sizes at static-init. A board variant changes these here.
static constexpr int LED_PIN   = solide::kBoardSolideS3.led.din;
static constexpr int LED_COUNT = solide::kBoardSolideS3.led.count;

// ----------------------------------------------------------------------------
// S3 WS2812B ring driver. Two render layers over one ~60 FPS task:
//   * single-ring Pattern (renderSingle) — boot/wifi/affirmation.
//   * agent-status segments (renderAgents) — the ring:: allocator's live
//     sessions, each arc animated by its status.
//
// Colour/brightness policy (per the Adafruit_NeoPixel gotchas): the per-segment
// and per-animation brightness is baked INTO the pixel RGB (non-destructive),
// and setBrightness() is called at most once per frame as a global cap — never
// per segment (it is a lossy in-place rescale).
//
// The render pacing is LED_FRAME_MS (~16 ms). The classic driver's 500 ms cap
// existed only to limit a per-show() RMT heap leak on the no-PSRAM ESP32; the S3
// (IDF RMT path + 8 MB PSRAM) has no such pressure, so the ring animates smooth.
// ----------------------------------------------------------------------------

namespace solide::leds {

static Adafruit_NeoPixel g_ring(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// Single-ring state (written from any task, read by the render task).
static volatile Pattern g_pat    = Pattern::Off;
static volatile uint8_t g_r = 0, g_g = 120, g_b = 255;
static volatile uint8_t g_bright = LED_BRIGHTNESS;         // MAX brightness; all output maps within [0,g_bright]
static volatile ring::Scheme g_scheme = ring::Scheme::Rainbow;  // ambient cycle palette
static volatile bool    g_taskAlive = false;

// Agent-status state: the allocator + a spinlock. The render task takes a short
// snapshot under the lock each frame, then renders from the copy (no long hold).
static ring::Allocator  g_alloc;
static portMUX_TYPE     g_mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t     g_task = nullptr;   // for stack high-water diagnostics

// Animation tuning.
static constexpr uint16_t BREATHE_MS   = 2600;  // idle / HITL breathe period
static constexpr uint16_t BLINK_MS     = 300;   // approval blink period
static constexpr uint16_t FADE_MS      = 1500;  // done fade duration
static constexpr uint16_t COMET_STEP_MS= 55;    // running comet head speed
static constexpr int      COMET_TAIL   = 5;     // comet tail length (LEDs)
static constexpr uint8_t  FLOOR_DIM    = 10;    // faint segment floor (keeps arc visible)
static constexpr uint8_t  DONE_EMBER   = 26;    // steady dim green after a Done fade
static constexpr uint8_t  ACCENT_LEVEL = 210;   // provider-marker pixel brightness

// ---- colour helpers ---------------------------------------------------------

// hue 0-254 -> gamma-corrected colour wheel; 255 == white.
static uint32_t hueColor(uint8_t hue) {
  if (hue == 255) return g_ring.Color(255, 255, 255);
  return g_ring.gamma32(g_ring.ColorHSV((uint16_t)(hue * 257)));
}
static inline uint8_t chR(uint32_t c) { return (c >> 16) & 0xFF; }
static inline uint8_t chG(uint32_t c) { return (c >> 8) & 0xFF; }
static inline uint8_t chB(uint32_t c) { return c & 0xFF; }

// Write a scaled colour to a ring LED (index wraps).
static inline void putScaled(int idx, uint8_t r, uint8_t g, uint8_t b, uint8_t lvl) {
  idx %= LED_COUNT;
  g_ring.setPixelColor(idx, g_ring.Color((r * lvl) / 255, (g * lvl) / 255, (b * lvl) / 255));
}

// ---- single-ring patterns ---------------------------------------------------

static void renderSingle(uint32_t t) {
  uint8_t r = g_r, gv = g_g, b = g_b;
  g_ring.setBrightness(g_bright);
  switch (g_pat) {
    case Pattern::Off:
      g_ring.clear();
      break;

    case Pattern::Solid:
      for (int i = 0; i < LED_COUNT; i++) g_ring.setPixelColor(i, g_ring.Color(r, gv, b));
      break;

    case Pattern::Spinner: {
      g_ring.clear();
      uint16_t head = (t / 40) % LED_COUNT;
      for (int k = 0; k < 6; k++) {
        int i = (head - k + LED_COUNT) % LED_COUNT;
        putScaled(i, r, gv, b, 255 >> k);
      }
      break;
    }

    case Pattern::Pulse: {
      uint8_t lvl = ring::breatheLevel(t, 2000);
      for (int i = 0; i < LED_COUNT; i++) putScaled(i, r, gv, b, lvl);
      break;
    }

    case Pattern::Rainbow: {
      // Smooth ambient cycle: phase advances continuously with millis(), so motion
      // is fluid regardless of frame timing. The selected scheme colours it — the
      // full HSV rainbow (default) or a curated palette (ring::schemeColor).
      ring::Scheme sc = g_scheme;
      uint32_t offset = (t / 6) & 0xFFFF;
      for (int i = 0; i < LED_COUNT; i++) {
        uint16_t phase = (uint16_t)(offset * 256 + (uint32_t)i * 65536 / LED_COUNT);
        uint32_t c;
        if (sc == ring::Scheme::Rainbow) {
          c = g_ring.gamma32(g_ring.ColorHSV(phase));            // approved full-wheel path
        } else {
          ring::RGB p = ring::schemeColor(sc, phase);
          c = g_ring.gamma32(g_ring.Color(p.r, p.g, p.b));
        }
        g_ring.setPixelColor(i, c);
      }
      break;
    }

    case Pattern::Flash: {
      bool on = (t % 400) < 200;
      for (int i = 0; i < LED_COUNT; i++)
        g_ring.setPixelColor(i, on ? g_ring.Color(r, gv, b) : 0);
      break;
    }
  }
}

// ---- agent-status segments --------------------------------------------------

static void renderSeg(int start, int len, const ring::Slot& s, uint32_t t) {
  if (len <= 0) return;
  ring::Style st = ring::styleFor(s.status);
  uint32_t base = hueColor(st.hue);
  uint8_t r = chR(base), g = chG(base), b = chB(base);

  switch (st.anim) {
    case ring::Anim::Off:
      break;   // leave dark

    case ring::Anim::Solid:
      for (int k = 0; k < len; k++) putScaled(start + k, r, g, b, 255);
      break;

    case ring::Anim::Breathe: {
      uint8_t lvl = ring::breatheLevel(t, BREATHE_MS);
      for (int k = 0; k < len; k++) putScaled(start + k, r, g, b, lvl);
      break;
    }

    case ring::Anim::Blink: {
      uint8_t lvl = ring::blinkOn(t, BLINK_MS) ? 255 : FLOOR_DIM;
      for (int k = 0; k < len; k++) putScaled(start + k, r, g, b, lvl);
      break;
    }

    case ring::Anim::Comet: {
      int head = ring::cometHead(t, len, COMET_STEP_MS);
      int tail = (len - 1 < COMET_TAIL) ? (len - 1) : COMET_TAIL;  // keep a dark pixel on short arcs
      if (tail < 1) tail = 1;
      for (int k = 0; k < len; k++) {
        int dist = (head - k + len) % len;
        uint8_t f = ring::cometFalloff(dist, tail);
        putScaled(start + k, r, g, b, f > FLOOR_DIM ? f : FLOOR_DIM);
      }
      break;
    }

    case ring::Anim::Fade: {
      uint32_t elapsed = t - s.enteredAt;
      uint8_t f = ring::fadeLevel(elapsed, FADE_MS);
      uint8_t lvl = f > DONE_EMBER ? f : DONE_EMBER;   // settle to a dim ember
      for (int k = 0; k < len; k++) putScaled(start + k, r, g, b, lvl);
      break;
    }
  }

  // Optional provider accent: a bright marker pixel at the segment head.
  if (s.hasAccent && len > 0) {
    uint32_t ac = hueColor(s.accentHue);
    putScaled(start, chR(ac), chG(ac), chB(ac), ACCENT_LEVEL);
  }
}

static void renderAgents(uint32_t t, const ring::Slot* slots, int n) {
  g_ring.setBrightness(g_bright);
  g_ring.clear();
  ring::Span spans[RING_MAX_SEGMENTS];
  int m = ring::layout(LED_COUNT, n, LED_SEG_GAP, spans, RING_MAX_SEGMENTS);
  for (int i = 0; i < m; i++) renderSeg(spans[i].start, spans[i].len, slots[i], t);
}

// ---- render task ------------------------------------------------------------

static void task(void*) {
  g_taskAlive = true;
  ring::Slot snap[RING_MAX_SEGMENTS];
  for (;;) {
    uint32_t t = millis();
    portENTER_CRITICAL(&g_mux);
    int n = g_alloc.snapshot(snap, RING_MAX_SEGMENTS);
    portEXIT_CRITICAL(&g_mux);

    if (n > 0) renderAgents(t, snap, n);
    else       renderSingle(t);
    g_ring.show();
    vTaskDelay(pdMS_TO_TICKS(LED_FRAME_MS));
  }
}

// ---- public API -------------------------------------------------------------

bool begin() {
  if (g_task) return true;   // idempotent — a second call is a safe no-op
  g_ring.begin();
  g_bright = (uint8_t)LED_BRIGHTNESS;
  g_ring.setBrightness(g_bright);
  g_ring.clear();
  g_ring.show();
  // The render task is the SOLE caller of g_ring.show(): if it fails to start
  // the ring stays dark forever and nothing else notices — surface it. Stack is
  // 6 KB: the RMT show() path + float envelopes (cosf) want headroom; watch the
  // actual high-water mark via stackHighWaterBytes() during bring-up.
  BaseType_t ok = xTaskCreatePinnedToCore(task, "leds", 6144, nullptr, 1, &g_task, 1);
  if (ok != pdPASS) {
    Serial.printf("leds: task create FAILED heap=%u max8=%u\n",
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    return false;
  }
  return true;
}

bool taskAlive() { return g_taskAlive; }

// Minimum free stack the render task has ever had, in bytes. Watch this on
// hardware while exercising every pattern + 8 segments; if it dips near 0 the
// 6 KB stack is too small (a stack overflow here corrupts adjacent memory).
uint32_t stackHighWaterBytes() {
  return g_task ? uxTaskGetStackHighWaterMark(g_task) * sizeof(StackType_t) : 0;
}

void setBrightness(uint8_t b) { g_bright = b; }   // MAX brightness; everything scales within [0,b]
uint8_t maxBrightness() { return g_bright; }

void setScheme(ring::Scheme s) {
  if (s != g_scheme) Serial.printf("led: scheme -> %s\n", ring::schemeName(s));
  g_scheme = s;
}
ring::Scheme scheme() { return g_scheme; }

void show(Pattern p, uint8_t r, uint8_t gv, uint8_t b) {
  if (p != g_pat) Serial.printf("led: -> %s\n", patternName(p));
  g_r = r; g_g = gv; g_b = b; g_pat = p;
}

void off() {
  if (g_pat != Pattern::Off) Serial.println("led: -> off");
  g_pat = Pattern::Off;
}

// ---- agent-status API (thread-safe over the allocator) ----------------------

bool agentStatus(uint32_t key, ring::Status st) {
  uint32_t now = millis();               // hoist out of the critical section
  portENTER_CRITICAL(&g_mux);
  int idx = g_alloc.upsert(key, st, now);
  portEXIT_CRITICAL(&g_mux);
  // idx == -1 for Offline (freed) is expected; only a full-ring refusal is a
  // failure the caller may care about.
  return !(idx < 0 && st != ring::Status::Offline);
}

void agentAccent(uint32_t key, uint8_t hue) {
  portENTER_CRITICAL(&g_mux);
  g_alloc.setAccent(key, hue);
  portEXIT_CRITICAL(&g_mux);
}

void agentProgress(uint32_t key, uint8_t pct) {
  portENTER_CRITICAL(&g_mux);
  g_alloc.setProgress(key, pct);
  portEXIT_CRITICAL(&g_mux);
}

void agentClear() {
  portENTER_CRITICAL(&g_mux);
  g_alloc.clear();
  portEXIT_CRITICAL(&g_mux);
}

int agentCount() {
  portENTER_CRITICAL(&g_mux);
  int n = g_alloc.count();
  portEXIT_CRITICAL(&g_mux);
  return n;
}

// ---- debug / self-test ------------------------------------------------------

const char* patternName(Pattern p) {
  switch (p) {
    case Pattern::Off:     return "off";
    case Pattern::Solid:   return "solid";
    case Pattern::Spinner: return "spinner";
    case Pattern::Pulse:   return "pulse";
    case Pattern::Rainbow: return "rainbow";
    case Pattern::Flash:   return "flash";
  }
  return "unknown";
}

State currentState() {
  State s;
  s.pattern   = (uint8_t)g_pat;
  s.r         = g_r;
  s.g         = g_g;
  s.b         = g_b;
  s.bright    = g_bright;
  s.taskAlive = g_taskAlive;
  portENTER_CRITICAL(&g_mux);
  s.segCount  = g_alloc.count();
  portEXIT_CRITICAL(&g_mux);
  return s;
}

}  // namespace solide::leds
