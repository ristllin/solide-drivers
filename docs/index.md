# solide-drivers docs

A hardware-only board-support package for the **Solide S3** device
(ESP32-S3-DevKitC-1 N16R8). Import it and every peripheral is available under
`solide::` — no app, network, or product logic.

- [getting-started.md](getting-started.md) — import into your firmware + a minimal sketch
- [hardware.md](hardware.md) — pin map, power rails, refresh timings, caveats
- [architecture.md](architecture.md) — portable/device split, conventions, threading
- [testing.md](testing.md) — native tests, the serial `TEST` protocol, device pytest
- [modernization.md](modernization.md) — the Arduino-ESP32 3.x / IDF 5.x toolchain record
- [manifest.json](manifest.json) — machine-readable pins + API (for agents)
- **peripherals/** — per-driver API + example + limitations:
  [display](peripherals/display.md) ·
  [leds](peripherals/leds.md) ·
  [audio](peripherals/audio.md) ·
  [storage](peripherals/storage.md) ·
  [memory](peripherals/memory.md) ·
  [input](peripherals/input.md)

See also [`../AGENTS.md`](../AGENTS.md) (agent entry point) and [`../examples/`](../examples).
