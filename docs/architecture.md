# Architecture

## Two tiers, one repo

- **`src/portable/`** - pure logic, no Arduino/ESP deps, compiled on the host by
  `pio test -e native` (via `test_build_src`). This is where the decisions live and
  get unit-tested: `ring` (segment allocator + layout + animation math + palettes),
  `input` (quadrature decoder + button debounce), `menu` (FSM), `wav` (RIFF parse/
  build), `tone` (Goertzel + RMS).
- **`src/device/`** - ESP32-only drivers that turn those decisions into hardware:
  `display`, `leds`, `audio`, `storage`, `memory`, `input`, `selftest`, `board`.

Public headers are all under `include/solide/`; consumers `#include <solide/…>`.

## Conventions

- **Namespaces:** everything under `solide::` (`solide::display`, `solide::leds`,
  `solide::ring`, `solide::audio`, `solide::storage`, `solide::memory`,
  `solide::input`, `solide::menu`, `solide::art`, `solide::selftest`).
- **Board as data:** `include/solide/board.h` defines a `constexpr solide::Board`
  (pin groups); `board_solide_s3.h` is the one instance. Drivers read pins from it
  (compile-time constants), so a future board variant is a new constant + a new
  `-DSOLIDE_BOARD`, with zero driver changes.
- **Lifecycle:** each peripheral has `begin() -> bool`, a liveness check
  (`taskAlive()`/`ok()`), and participates in `solide::begin()` (aggregate) and the
  `selftest` protocol. Task-backed drivers (display, leds, input) render/poll on a
  pinned FreeRTOS task; the public API is non-blocking and thread-safe.
- **Dependency inversion:** `audio`/`storage`/`memory` take `fs::FS&` or use the SD
  singleton - no hard-wired transport. `display` is content-agnostic
  (`requestText`/`requestBitmap`/`requestMenu`) - no app/branding/network concepts.
- **Graceful degradation:** absent hardware (no SD card, no mic) returns
  `false`/`0`/`SKIP`, never crashes.

## Threading notes
- `leds`: a ~60 FPS render task; the public API mutates a `ring::Allocator` under a
  `portMUX` spinlock, and the task renders from a snapshot taken under the lock.
- `display`: an 8-entry command queue drained by a render task (the 3-colour panel
  takes ~18 s; callers never block).
- `audio`: synchronous; TX (I2S1) + RX (I2S0) are independent channels, so the
  acoustic loopback can play and record concurrently (full-duplex read/write).
