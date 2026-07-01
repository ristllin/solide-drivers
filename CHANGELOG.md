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
