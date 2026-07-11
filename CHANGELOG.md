# Changelog

All notable changes to solide-drivers are recorded here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/); versions follow semver.

## [0.2.1] — 2026-07-11

### Changed
- **Audio: mic converted PDM → I2S-std RX** for the new **INMP441 / ICS-43434**
  I2S MEMS microphone (replaces the PDM MEMS mic). Pins **SCK/BCLK 15 · WS 18 ·
  SD 16** (was PDM CLK 15 / DATA 16 — the new line is WS/LRCLK on GPIO 18; an
  L/R strap to GND selects the left slot). The board mic pin struct changed
  `{clk, data}` → `{bclk, ws, din}`. The speaker amp is now a **MAX98357A**
  I2S Class-D (was NS4168), with built-in thermal + over-current protection.
  **Public API unchanged** (`solide::audio` signatures identical). GPIO 18 is no
  longer a free spare.

## [0.2.0] — 2026-07-02

One release, two independent changes bundled together (see "Versioning" in
AGENTS.md): a new public API is a MINOR bump under semver-for-0.x even though
the audio change alone would only be a PATCH-level fix — bumping once for both
keeps the tag/version 1:1 with what actually shipped.

### Added
- **`solide::leds::showFrame(const ring::RGB*, size_t)` + `clearFrame()`** — a
  third LED render layer (raw per-pixel frame) alongside the existing single-
  ring Pattern and agent-status segment layers, for a caller with its own
  animation engine. Unblocks Nimbus's host-tested `nimbus::ring::Animator`
  (`lib/core/ring_animator.h`), which was built against this exact API name
  and shape but shipped dark pending this upstream addition (see Nimbus's
  `docs/led-ux.md`). Design (full reasoning in `include/solide/leds.h`):
  showFrame() is the highest-precedence layer and takes over on its first
  call without requiring the caller to first clear Pattern/agent state;
  release is explicit via `clearFrame()` (mirrors `off()`); a caller that
  stops pushing frames (crash/deadlock) auto-releases raw-frame mode after
  `LED_FRAME_STALE_MS` (500 ms) so the ring can't freeze on stale pixels
  forever; a `count` that doesn't match the board's LED count is clamped,
  never rejected (short frames leave the tail off, long frames drop the
  extra pixels). `leds::State` gains a `rawFrame` flag for self-test/debug
  visibility, alongside the existing `segCount`/`taskAlive`.
- **Audio self-test diagnostics** to bisect the open PDM-mic capture bug
  (see "Known" below): `audio::loopbackSelfTest()` now reports an `LbDiag`
  (tone magnitude, an off-tone control-band magnitude, RMS, peak, DC mean,
  sample count) instead of a bare pass/fail, so a `TEST audio` SKIP comes
  with an actionable reason (`MIC-DEAD` / `SPEAKER-OR-COUPLING` / below
  detection floor) instead of one aggregate number. Two new manual test
  console commands split the loopback in half: `TEST spk` (audible tone,
  no mic — confirms the amp/5V/speaker chain) and `TEST mic` (RMS monitor
  that responds to tapping, no speaker — confirms the mic capture path).
  Loudened the loopback test tone (0.24 -> 0.73 FS) for more detection
  margin over PDM self-noise. Does **not** fix the underlying dead-line bug
  (still open — needs the mic hardware in hand); this is diagnostic
  instrumentation to make that bug bisectable on real hardware.

## [0.1.0] — 2026-07-02

First consumable release — the full hardware layer for the Solide S3, on the modern
Arduino-ESP32 3.3.9 / IDF 5.5.4 toolchain. All drivers ported + validated (audio mic
capture pending a hardware check); 48 host tests + an on-device self-test protocol +
a device pytest harness; documented for humans and agents.

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
  ~314 KB. The whole non-audio driver layer runs on the modern toolchain.
- **Toolchain bump:** moved from pioarduino `53.03.13` (Arduino 3.1.3 / IDF 5.3) to
  **`55.03.39` (Arduino 3.3.9 / IDF 5.5.4)**. 3.1.3 has a regression that breaks the new
  I2S driver on PSRAM boards (`gdma: user context not in internal RAM`), fixed on 3.3.x.
  All of M0–M2 re-validated on 3.3.9 (`RESULT all PASS`). See `docs/modernization.md`.
- **M3 — audio rewrite (done):** `solide::audio` reimplemented on the ESP-IDF 5 channel
  API — `driver/i2s_std` (speaker TX) + `driver/i2s_pdm` (mic RX); public API unchanged.
  Configs built field-by-field (the IDF `*_DEFAULT_CONFIG` macros are C-designated-init,
  which breaks under C++). **TX hardware-validated** (init clean, `playPcm` plays beeps).
  **RX driver validated** (init + `i2s_channel_read` succeed at 16 kHz mono), but the mic
  reads a constant `-30935` = "no data on the data line" per ESP-IDF #12382 — a **mic
  hardware issue** (the original build never capture-validated the mic), not the driver.
  The acoustic loopback (M4) is gated on the mic delivering data. `examples/07_audio_play_record`.
- **M4 — acoustic loopback (code done):** `audio::loopbackSelfTest()` plays a tone on a
  concurrent TX task while reading the PDM RX (full-duplex), Goertzel-detects it, wired
  into `TEST audio`. Code-path validated (no crash, heap flat); acoustic PASS + threshold
  tuning pending the 5 V amp + a working mic.
- **M5 — device harness + CI (done):** `tools/device_harness.py` (serial-only) +
  `test_device/` pytest (drives the `TEST` protocol; 8/8 pass) + `tools/solide_console.py`
  recorder + `.github/workflows/native-tests.yml` (CI runs `pio test -e native`). The
  harness caught a real firmware crash (a concurrent `i2s_new_channel` race in the
  loopback), since fixed.
- **M6 — examples + docs + manifest (done):** examples 02/03/04/05/06/07/08 (one per
  peripheral + a combined demo, all compile); `docs/` (hardware, architecture,
  getting-started, testing, modernization, peripherals/*); `docs/manifest.json` +
  `tools/gen_manifest.py` (pins parsed from the board header); `AGENTS.md`.
- **Pre-release hardening:** adversarial review + verify pass — fixed a WAV
  chunk-size overflow (device hang on a crafted header), a display queue leak on
  task-create failure, non-idempotent `begin()`, and self-test residue; added a
  regression test. 48 host tests + 8 device tests green.

### Known
- **Mic capture** reads a constant `-30935` = no data on the PDM DATA line (ESP-IDF
  #12382) — a mic hardware/wiring issue to check (GPIO16 DATA / GPIO15 CLK / module).
  The RX driver is correct and will capture once the mic delivers data.
- LED lighting, speaker audibility, and the acoustic loopback need the **5 V bus** powered.
