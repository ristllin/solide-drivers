# Modernization record — Arduino-ESP32 3.x / ESP-IDF 5.x (pioarduino)

The package targets the **modern** Espressif toolchain (not the legacy Arduino
2.0.17 / IDF 4.4 the original firmware used), so the audio driver uses the modern
I2S channel API and the platform stays maintained.

## Working toolchain (validated on hardware 2026-07)

| Piece | Value |
|---|---|
| Platform | pioarduino `platform-espressif32` **55.03.39** |
| Arduino-ESP32 | **3.3.9** |
| ESP-IDF | **5.5.4** |
| PlatformIO Core | 6.1.19 |
| Board base def | `esp32-s3-devkitc-1` (resolves as N8/no-PSRAM; overridden below) |

`platformio.ini` (`[env:smoke]` / `[env:esp32s3]`):
```
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.39/platform-espressif32.zip
board = esp32-s3-devkitc-1
board_build.arduino.memory_type = qio_opi   ; N16R8: QIO flash + OCTAL PSRAM
board_upload.flash_size = 16MB
board_build.partitions = default_16MB.csv
build_flags = -DBOARD_HAS_PSRAM -DARDUINO_USB_CDC_ON_BOOT=1 -DARDUINO_USB_MODE=1
```

## The prior "tooling block" — resolved

The original project documented the pioarduino path as *tooling-blocked* (pioarduino
postinstall vs PlatformIO 6.1.19). That block does **not** recur — `pio run` builds
cleanly and flashes on PlatformIO 6.1.19. The base board def advertises "N8, No
PSRAM", but `memory_type=qio_opi` + `BOARD_HAS_PSRAM` correctly drive the N16R8;
runtime confirms 8 MB.

## Why 55.03.39 and not 53.03.13 (the I2S regression)

The package was first brought up on `53.03.13` (Arduino-ESP32 **3.1.3** / IDF 5.3) —
M0–M2 validated there. But 3.1.3 has a **regression** that breaks the new I2S driver
on PSRAM boards: `i2s_channel_init_std_mode`/`_pdm_rx_mode` fail with
`gdma: user context not in internal RAM` → `Register tx/rx callback failed` (the I2S
callback context lands in PSRAM, which GDMA rejects). It works in 3.1.1, broke in
3.1.2/3.1.3, and is fixed forward — so the package moved to the latest stable
**55.03.39 (Arduino 3.3.9 / IDF 5.5.4)**, where audio init succeeds. All of M0–M2
re-validated on 3.3.9 (`RESULT all PASS (6/6)`); the bump broke nothing. Refs:
[arduino-esp32 #11004](https://github.com/espressif/arduino-esp32/issues/11004),
[#11058](https://github.com/espressif/arduino-esp32/issues/11058).

## Hardware validation summary (on 3.3.9)

- **M0 smoke:** USB-CDC serial, PSRAM = 8 MB, GPIO toggle, ~350 KB heap.
- **M2 (`TEST all`):** led / epd (fast B/W ~2.2 s) / sd (14.9 GB) / memory (NVS+JSON) /
  input all PASS. Heap flat.
- **M3 audio TX:** `i2s_std` speaker — channel init succeeds, `playPcm` writes; beeps
  play (needs the 5 V amp bus).
- **M3 audio RX:** `i2s_pdm` mic — channel init + `i2s_channel_read` succeed (16000
  samples/s), but the mic reads a **constant `-30935`**. Per ESP-IDF
  [#12382](https://github.com/espressif/esp-idf/issues/12382) that value means "the
  data line is always 0" — i.e. **no PDM data on `din`**, a hardware problem (the mic
  module / its DATA solder, not the driver config, which matches the IDF known-good
  default). The original build only ever *compiled* the mic — capture was never
  validated. **Action: check the mic DATA (GPIO16) / CLK (GPIO15) wiring + module, or
  swap a known-good PDM breakout.** The RX driver is correct and will capture once the
  mic delivers data.

### PDM notes
- The PDM HP-filter fields (`hp_en` / `hp_cut_off_freq_hz` / `amplify_num`) are gated by
  `SOC_I2S_SUPPORTS_PDM_RX_HP_FILTER` — absent on the 3.1.3 libs, present on 3.3.9. The
  driver sets them under `#if SOC_I2S_SUPPORTS_PDM_RX_HP_FILTER` so it compiles either way.
- Config structs are initialized **field-by-field**, not via the IDF `*_DEFAULT_CONFIG`
  macros: those use C designated initializers whose field order differs from the struct
  order, which is an error under C++.

## Fallback (kept for the record)

If a future pioarduino bump breaks the build, pin back to `55.03.39`, or fall back to
stock `espressif32@7.0.1` (Arduino 2.0.17) + the legacy `driver/i2s.h` audio impl —
every other driver is framework-agnostic.
