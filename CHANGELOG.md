# Changelog

All notable changes to solide-drivers are recorded here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/); versions follow semver.

## [0.1.0-dev] — unreleased

### Added
- Repo scaffold: `library.json`, `platformio.ini` (`native` + `smoke` envs),
  `include/solide/board.h` + `boards/board_solide_s3.h` (pin map as a `constexpr
  Board` struct), MIT license, docs skeleton.
- **M0 — platform de-risk (done):** `examples/01_smoke_blink_serial` builds on
  pioarduino (Arduino-ESP32 3.1.3 / IDF 5.3) and is hardware-validated —
  USB-CDC serial, PSRAM = 8 MB, GPIO toggle. The prior pioarduino tooling block
  does not recur on release 53.03.13 + PlatformIO 6.1.19. See `docs/modernization.md`.
- **M1 — portable core + native tests (done):** the full host-testable core lives
  in `src/portable/` + `include/solide/`, renamed to `solide::` namespaces:
  `ring` (segmentation), `input` (QuadDecoder + Button), `menu` (FSM), plus two new
  modules the audio loopback needs — `wav` (RIFF parse/build) and `tone`
  (Goertzel + RMS/peak). **47 host tests pass** (`pio test -e native`), validating
  the package build system (`test_build_src` for `src/portable`).
- **M2 — device drivers (in progress):** device build env `[env:esp32s3]`
  (pioarduino) added. Ported to `solide::` + the `Board` pin struct: `leds`
  (**hardware-validated** on the modern platform — ring lights, and heap stays flat
  at ~343 KB across frames, confirming no per-show RMT leak on IDF5), `storage` (SD),
  `input` (encoder), and **`display`** (e-paper) — **hardware-validated**: fast B/W
  refresh `fastFull : 2212000 µs` (~2.2 s, identical to the original), WS_20_30 LUT
  subclass intact, heap flat. Display got the approved **API trim**: no app/branding
  `StatusInfo` — generic `requestText`/`requestMenu`/`showArt`/`requestBitmap`/`clear`.
  **`memory`** (NVS typed KV + SD JSON/blob under `/memory/`), **`selftest`** (the
  agent-drivable serial `TEST <name>` -> `RESULT <name> PASS|FAIL|SKIP` protocol),
  the `solide::begin()` aggregate + `solide.h` umbrella, and `examples/08_selftest_console`.
  **M2 done — `RESULT all PASS (6/6)` on hardware:** led/epd/sd/memory/input all PASS
  (SD 14.9 GB working, NVS+JSON round-trip), audio SKIP (deferred to M3). Heap flat
  ~314 KB. The whole non-audio driver layer runs on Arduino 3.1.3 / IDF 5.3.
