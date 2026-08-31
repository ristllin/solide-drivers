# Changelog

All notable changes to solide-drivers are recorded here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/); versions follow semver.

## v0.6.1 (2026-08-24)

- display_tft: full-frame blits sourced from PSRAM are staged band-by-band through
  an internal-SRAM bounce buffer. A single large DMA burst from PSRAM could reset
  the panel (white screen); internal-frame and bounce-unavailable paths are
  unchanged.

## v0.6.0 (2026-08-24)

- E-paper driver is compile-gated behind SOLIDE_HAS_EPAPER (default off). Colour-TFT
  builds link zero GxEPD2 and reclaim ~14.5 KB of contiguous internal SRAM (the two
  e-paper objects were file-scope statics the linker could not strip). Define
  SOLIDE_HAS_EPAPER=1 to keep building the e-paper API; the public header surface is
  unchanged either way.

## [0.8.0] - 2026-08-31

### Added
- **`solide::touch::health()`** and **`solide::touch::powerMode()`** - visibility into
  the capacitive (FT6336U/I2C) controller for firmware and tests. `health()` returns a
  `solide::touch::Health` (failures, recoveries, busClears, hardResets,
  consecutiveFailures, lastRecoveryMs, degraded); `powerMode()` returns the last-read
  0xA5 PWR_MODE byte. Both are inert (zero / 0xFF) on a resistive board.
- **`solide/touch_liveness.h`** - portable, host-tested `solide::touch::Liveness` recovery
  policy (new `test_touch_liveness`, 9 cases).

### Fixed
- **Capacitive touch silently dying after long idle (root-cause of the CUM-248 signature:
  firmware healthy, screen blanked by the saver, touch dead until power-cycle).** The
  FT6336U path in `touch.cpp` previously returned a bare `false` on ANY I2C failure with
  no recovery, no re-probe, and no visibility, so a controller that dropped off the shared
  bus after long idle stayed dead forever. Now:
  - The read distinguishes a completed transaction (finger or not - both healthy) from a
    bus fault, and counts consecutive faults.
  - After K=4 consecutive faults a recovery ladder runs: I2C bus-clear (9 SCL pulses +
    STOP, safe on the codec-shared bus), then a TC_RST hardware re-reset + re-probe, then
    exponential backoff (100 ms doubling to 4 s) so a truly-absent controller is not
    hammered. Any successful transaction resets the streak and counts a recovery.
  - At begin() (and after each hard reset) the driver reads 0xA5 PWR_MODE for visibility
    and writes 0x86 CTRL = 0 to keep the controller in Active mode, disabling the
    datasheet-documented monitor-mode auto-entry that can make it unresponsive when idle.
  Resistive (XPT2046) boards are unaffected.

## [0.7.2] - 2026-08-30

### Fixed
- **`display_tft::healthy()`** now compares the full RDDST status byte (minus the
  refresh scan-toggle bit0) against `madctlFor(g_flip)` with the MY/MX flip bits
  cleared, instead of masking the flip bits out of both sides. RDDST reports MY/MX
  as their power-on 0 regardless of the flip written, so the expected readback is
  0x28 for both orientations: this keeps the v0.7.1 no-thrash property (a flipped
  panel no longer reads unhealthy forever) while restoring detection of a partial
  state loss that raises MY/MX in RDDST (got=0xE8), which the v0.7.1 0x3E mask read
  as healthy. Reset (0x00) still fails in both orientations. Regression-tested both
  directions on the host (nimbus `test/test_panel_health`).

## [0.7.1] - 2026-08-28

### Fixed
- **`display_tft::healthy()`** masked the MY/MX flip bits out of the RDDST compare
  so a flipped panel (`madctlFor(1)=0xE8`) no longer read unhealthy forever and
  thrashed the panel watchdog (nimbus CUM-188). Superseded by 0.7.2, which keeps the
  no-thrash property without dropping fault sensitivity.

## [0.7.0] - 2026-08-25

### Added
- **`solide::storage::format()`** - full-card FAT (re)format primitive. Runs
  FATFS `f_mkfs` (FAT or FAT32, chosen by card size) over the mounted card on
  whichever backend actually mounted (SPI `SD` or on-board
  `SD_MMC`), then remounts so the card is left clean and ready for I/O. Refuses
  cleanly (`FormatResult::NoCard`, no side effects) when no card is mounted - it
  never formats a card it did not already have open. Returns a `FormatResult`
  (`Ok` / `NoCard` / `MkfsFailed` / `RemountFailed`) naming the exact failure
  stage. The refuse-when-absent guard and the mkfs-then-remount ordering live in
  a portable, host-tested state machine (`solide/storage_format.h`,
  `test/test_storage_format/`); the on-card destructive acceptance is gated on a
  scratch card and lives in the consuming firmware's HIL suite.

## [0.4.0] - 2026-08-10

### Added
- **`solide::display_tft` + `solide::touch`** - a second display/input pair: 2.8"
  ILI9341 colour TFT (240×320) + XPT2046 resistive touch, as an alternative to
  the e-paper + encoder. `solide::begin()` chooses the fitted pair (defaulting
  to the e-paper); the self-test checks the pair that is actually FITTED.
  Includes: landscape geometry with a runtime flip, a reset-free `rearm()`
  (also clears partial mode, inversion and scroll), `healthy()` panel-register
  self-check to detect a silently-reset panel, a direct pixel-path check (GRAM
  round-trip + full-frame readback), backlight-PWM attach verification,
  bounce-buffered blit + panel readback helpers, atomic touch-calibration
  read/write, and touch axes rotated to match the landscape panel with
  shared-MISO diagnostics.
- **E-paper partial (differential) refresh** for `requestBitmap` - flash-free
  bitmap updates.
- **`SOLIDE_NO_COLOR_EINK` build flag** - drops the unused 3-colour e-ink
  instance (~9.6 KB of contiguous internal SRAM) and renders colour requests as
  B/W. `NIMBUS_NO_COLOR_EINK` is kept as a working alias.

### Fixed
- **E-paper de-ghosting**: red RAM cleared on B/W-mode entry and blanked inside
  the ghost-clear itself (fixes the stuck-red 3-colour panel after a
  ghost-clear); the ghost-clear now uses B/W inversion flashes instead of the
  slow OTP waveform.
- **Onboard RGB LED cleared at boot** - it shares a pin with touch-CS and was
  never told to go dark, so it sat lit from the bootloader on.
- **Audio**: the shared speaker TX channel is serialised with a recursive mutex.

## [0.3.1] - 2026-07-15

### Fixed
- **Input: encoder A/B swapped** - the knob turned the wrong way on this wiring
  (owner field report). One decode point flips menu + cursor together.

## [0.3.0] - 2026-07-15

First tag carrying the 0.2.1 audio changes below (0.2.1 was a CHANGELOG entry
that never got its own tag), plus:

### Added
- **Audio: master playback volume** - `setVolume`/`getVolume`.
- **Battery: pack-voltage sensing driver** (ADC1 divider) + selftest check +
  `Board.batt` pin entry.
- **Storage: `end()`** - unmounts the SD and clears the mount latch.
- **Display: true full-update (ghost-clear) path** for `requestBitmap`, with the
  busy timeout raised so the full update can finish.

### Changed
- **Input: long-press threshold** raised 400 → 600 ms.
- **Ring: a lone segment renders as an arc**, not a full circle (the layout
  reserves a gap).

## [0.2.1] - 2026-07-11

### Changed
- **Audio: mic converted PDM → I2S-std RX** for the new **INMP441 / ICS-43434**
  I2S MEMS microphone (replaces the PDM MEMS mic). Pins **SCK/BCLK 15 · WS 18 ·
  SD 16** (was PDM CLK 15 / DATA 16 - the new line is WS/LRCLK on GPIO 18; an
  L/R strap to GND selects the left slot). The board mic pin struct changed
  `{clk, data}` → `{bclk, ws, din}`. The speaker amp is now a **MAX98357A**
  I2S Class-D (was NS4168), with built-in thermal + over-current protection.
  **Public API unchanged** (`solide::audio` signatures identical). GPIO 18 is no
  longer a free spare.

## [0.2.0] - 2026-07-02

One release, two independent changes bundled together (see "Versioning" in
AGENTS.md): a new public API is a MINOR bump under semver-for-0.x even though
the audio change alone would only be a PATCH-level fix - bumping once for both
keeps the tag/version 1:1 with what actually shipped.

### Added
- **`solide::leds::showFrame(const ring::RGB*, size_t)` + `clearFrame()`** - a
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
  no mic - confirms the amp/5V/speaker chain) and `TEST mic` (RMS monitor
  that responds to tapping, no speaker - confirms the mic capture path).
  Loudened the loopback test tone (0.24 -> 0.73 FS) for more detection
  margin over PDM self-noise. Does **not** fix the underlying dead-line bug
  (still open - needs the mic hardware in hand); this is diagnostic
  instrumentation to make that bug bisectable on real hardware.

## [0.1.0] - 2026-07-02

First consumable release - the full hardware layer for the Solide S3, on the modern
Arduino-ESP32 3.3.9 / IDF 5.5.4 toolchain. All drivers ported + validated (audio mic
capture pending a hardware check); 48 host tests + an on-device self-test protocol +
a device pytest harness; documented for humans and agents.

### Added
- Repo scaffold: `library.json`, `platformio.ini` (`native` + `smoke` envs),
  `include/solide/board.h` + `boards/board_solide_s3.h` (pin map as a `constexpr
  Board` struct), MIT license, docs skeleton.
- **M0 - platform de-risk (done):** `examples/01_smoke_blink_serial` builds on
  pioarduino (Arduino-ESP32 3.1.3 / IDF 5.3) and is hardware-validated -
  USB-CDC serial, PSRAM = 8 MB, GPIO toggle. The prior pioarduino tooling block
  does not recur on release 53.03.13 + PlatformIO 6.1.19. See `docs/modernization.md`.
- **M1 - portable core + native tests (done):** the full host-testable core lives
  in `src/portable/` + `include/solide/`, renamed to `solide::` namespaces:
  `ring` (segmentation), `input` (QuadDecoder + Button), `menu` (FSM), plus two new
  modules the audio loopback needs - `wav` (RIFF parse/build) and `tone`
  (Goertzel + RMS/peak). **47 host tests pass** (`pio test -e native`), validating
  the package build system (`test_build_src` for `src/portable`).
- **M2 - device drivers (in progress):** device build env `[env:esp32s3]`
  (pioarduino) added. Ported to `solide::` + the `Board` pin struct: `leds`
  (**hardware-validated** on the modern platform - ring lights, and heap stays flat
  at ~343 KB across frames, confirming no per-show RMT leak on IDF5), `storage` (SD),
  `input` (encoder), and **`display`** (e-paper) - **hardware-validated**: fast B/W
  refresh `fastFull : 2212000 µs` (~2.2 s, identical to the original), WS_20_30 LUT
  subclass intact, heap flat. Display got the approved **API trim**: no app/branding
  `StatusInfo` - generic `requestText`/`requestMenu`/`showArt`/`requestBitmap`/`clear`.
  **`memory`** (NVS typed KV + SD JSON/blob under `/memory/`), **`selftest`** (the
  agent-drivable serial `TEST <name>` -> `RESULT <name> PASS|FAIL|SKIP` protocol),
  the `solide::begin()` aggregate + `solide.h` umbrella, and `examples/08_selftest_console`.
  **M2 done - `RESULT all PASS (6/6)` on hardware:** led/epd/sd/memory/input all PASS
  (SD 14.9 GB working, NVS+JSON round-trip), audio SKIP (deferred to M3). Heap flat
  ~314 KB. The whole non-audio driver layer runs on the modern toolchain.
- **Toolchain bump:** moved from pioarduino `53.03.13` (Arduino 3.1.3 / IDF 5.3) to
  **`55.03.39` (Arduino 3.3.9 / IDF 5.5.4)**. 3.1.3 has a regression that breaks the new
  I2S driver on PSRAM boards (`gdma: user context not in internal RAM`), fixed on 3.3.x.
  All of M0–M2 re-validated on 3.3.9 (`RESULT all PASS`). See `docs/modernization.md`.
- **M3 - audio rewrite (done):** `solide::audio` reimplemented on the ESP-IDF 5 channel
  API - `driver/i2s_std` (speaker TX) + `driver/i2s_pdm` (mic RX); public API unchanged.
  Configs built field-by-field (the IDF `*_DEFAULT_CONFIG` macros are C-designated-init,
  which breaks under C++). **TX hardware-validated** (init clean, `playPcm` plays beeps).
  **RX driver validated** (init + `i2s_channel_read` succeed at 16 kHz mono), but the mic
  reads a constant `-30935` = "no data on the data line" per ESP-IDF #12382 - a **mic
  hardware issue** (the original build never capture-validated the mic), not the driver.
  The acoustic loopback (M4) is gated on the mic delivering data. `examples/07_audio_play_record`.
- **M4 - acoustic loopback (code done):** `audio::loopbackSelfTest()` plays a tone on a
  concurrent TX task while reading the PDM RX (full-duplex), Goertzel-detects it, wired
  into `TEST audio`. Code-path validated (no crash, heap flat); acoustic PASS + threshold
  tuning pending the 5 V amp + a working mic.
- **M5 - device harness + CI (done):** `tools/device_harness.py` (serial-only) +
  `test_device/` pytest (drives the `TEST` protocol; 8/8 pass) + `tools/solide_console.py`
  recorder + `.github/workflows/native-tests.yml` (CI runs `pio test -e native`). The
  harness caught a real firmware crash (a concurrent `i2s_new_channel` race in the
  loopback), since fixed.
- **M6 - examples + docs + manifest (done):** examples 02/03/04/05/06/07/08 (one per
  peripheral + a combined demo, all compile); `docs/` (hardware, architecture,
  getting-started, testing, modernization, peripherals/*); `docs/manifest.json` +
  `tools/gen_manifest.py` (pins parsed from the board header); `AGENTS.md`.
- **Pre-release hardening:** adversarial review + verify pass - fixed a WAV
  chunk-size overflow (device hang on a crafted header), a display queue leak on
  task-create failure, non-idempotent `begin()`, and self-test residue; added a
  regression test. 48 host tests + 8 device tests green.

### Known
- **Mic capture** reads a constant `-30935` = no data on the PDM DATA line (ESP-IDF
  #12382) - a mic hardware/wiring issue to check (GPIO16 DATA / GPIO15 CLK / module).
  The RX driver is correct and will capture once the mic delivers data.
- LED lighting, speaker audibility, and the acoustic loopback need the **5 V bus** powered.
