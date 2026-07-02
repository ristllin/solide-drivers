# solide::leds — WS2812B ring (45 px)

A ~60 FPS render task drives the ring. Two layers: single-ring **patterns** and
per-session **status segments** (backed by the host-tested `solide::ring` core).

## API
```cpp
bool begin();
bool taskAlive();
uint32_t stackHighWaterBytes();
void setBrightness(uint8_t maxB);   uint8_t maxBrightness();   // global cap; everything scales within [0,max]
void setScheme(ring::Scheme s);     ring::Scheme scheme();     // ambient-cycle palette (Rainbow/Pastel/Ocean/…)

enum class Pattern { Off, Solid, Spinner, Pulse, Rainbow, Flash };
void show(Pattern p, uint8_t r = 0, uint8_t g = 120, uint8_t b = 255);
void off();

// agent-status segmentation:
bool agentStatus(uint32_t key, ring::Status st);   // Running/AwaitingApproval/WaitingInput/Done/Error/Idle/Offline
void agentAccent(uint32_t key, uint8_t hue);        // provider marker
void agentProgress(uint32_t key, uint8_t pct);
void agentClear();   int agentCount();
```
`ring::Status → colour+animation` is byte-compatible with the nuage-solide-notify model.

## Example
`examples/03_led_ring` (patterns + schemes + status arcs), `examples/99_combined_demo`.

## Limitations
- **Needs the 5 V bus to light** (3.3 V logic on GPIO21). ~372 mA worst case @ brightness 30.
- Global brightness is a lossy scale; per-segment brightness is baked into the pixels.
- Up to `RING_MAX_SEGMENTS` (8) concurrent status segments; a higher-priority status
  evicts the lowest-priority one when full.
