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
- **M1 (in progress):** portable `solide::ring` core ported to `src/portable/` +
  `include/solide/ring.h` with 26 host tests passing (`pio test -e native`).
