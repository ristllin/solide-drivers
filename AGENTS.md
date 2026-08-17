# AGENTS.md - solide-drivers

Machine entry point for agents working on or with this package.

## What this is

A **hardware-only board-support package** for the **Solide S3** device
(ESP32-S3-DevKitC-1 N16R8). It exposes every peripheral under `solide::` and
nothing else - **no app, agent, network, or business logic belongs here.** If a
change reaches for WiFi, HTTP, an LLM, or a product concept, it's in the wrong
repo.

## The device at a glance

Full structured hardware map: [`docs/manifest.json`](docs/manifest.json). Human
reference: [`docs/hardware.md`](docs/hardware.md). Pin source of truth:
`include/solide/boards/board_solide_s3.h` (a `constexpr solide::Board`).

Peripherals → namespaces: `solide::display` (e-paper), `solide::leds` (WS2812B ring
+ `solide::ring` status segmentation), `solide::audio` (I2S speaker + I2S-std
MEMS mic, INMP441/ICS-43434),
`solide::storage` (SD), `solide::memory` (NVS + SD JSON), `solide::input` (encoder
+ `solide::menu`).

## Build & test

```bash
pio test -e native                              # host unit tests (portable core) - MUST stay green
SOLIDE_EXAMPLE=08_selftest_console pio run -e esp32s3 -t upload   # a device example
```
Toolchain is pinned in `platformio.ini` (pioarduino 55.03.39 = Arduino-ESP32 3.3.9
/ IDF 5.5.4). Before any `-t upload`, free the serial port:
`lsof -t /dev/cu.usbmodem* | xargs kill`.

## Verifying peripherals (serial protocol)

Flash `examples/08_selftest_console`, then over serial:

```
TEST all      -> RESULT led PASS ...  / RESULT epd PASS ... / ... / RESULT all PASS (n/m)
TEST <name>   -> RESULT <name> PASS|FAIL|SKIP <k=v>...     (name: led epd sd memory input audio)
INFO          -> INFO board=... psramMB=... heap=... uptime=...
```
`SKIP` = optional hardware absent (e.g. no SD card), not a failure.

## Punctuation

Never use an em dash (U+2014) anywhere in this repo: not in docs, comments,
commit messages, or UI strings. Use a hyphen, a comma, or a colon instead.

## Rules

- Hardware only. Keep the portable, host-testable logic in `src/portable/` (compiled
  by `pio test -e native` via `test_build_src`); Arduino-only code in `src/device/`.
- Add a test for portable logic; add a `selftest` check + `TEST` case for a new peripheral.
- Bump `CHANGELOG.md` on behaviour changes. Commit messages: no `Co-Authored-By`.
- Validate on hardware, don't eyeball - the `RESULT ... PASS` lines and refresh timings
  are the evidence.

## Versioning

Every meaningful change: bump `"version"` in `library.json`, add a
`CHANGELOG.md` entry describing it, and tag the commit - `git tag -a vX.Y.Z -m
"..."` - so a consumer (Nimbus or otherwise) can pin `lib_deps` to a specific
tag instead of tracking `main`. Semver-for-0.x: MAJOR stays `0` pre-1.0;
MINOR bumps for a new/changed public API (breaking or not); PATCH for a
fix/internal change with no API surface change. One release can bundle
multiple logical commits under one bump - just say so in the CHANGELOG entry.

## Known hardware caveats

- LED ring + audio amp need the **5 V** bus; everything else runs on 3.3 V.
- Audio board VCC is **3.3 V only** - the mic's DATA line follows VCC; 5 V damages the S3.
- Octal PSRAM occupies GPIO 33–37 (the N16R8 gotcha) - never assign them.
