# Modernization record — Arduino-ESP32 3.x / ESP-IDF 5.x (pioarduino)

The package targets the **modern** Espressif toolchain (not the legacy Arduino
2.0.17 / IDF 4.4 the original firmware used), so the audio driver uses the modern
I2S channel API and the platform stays maintained.

## Working toolchain (validated on hardware 2026-07)

| Piece | Value |
|---|---|
| Platform | pioarduino `platform-espressif32` **53.03.13** |
| Arduino-ESP32 | **3.1.3** |
| ESP-IDF | **5.3.0** |
| PlatformIO Core | 6.1.19 |
| Board base def | `esp32-s3-devkitc-1` (resolves as N8/no-PSRAM; overridden below) |
| esptool | 4.8.6 |

`platformio.ini` (`[env:smoke]` / device envs):
```
platform = https://github.com/pioarduino/platform-espressif32/releases/download/53.03.13/platform-espressif32.zip
board = esp32-s3-devkitc-1
board_build.arduino.memory_type = qio_opi   ; N16R8: QIO flash + OCTAL PSRAM
board_upload.flash_size = 16MB
board_build.partitions = default_16MB.csv
build_flags = -DBOARD_HAS_PSRAM -DARDUINO_USB_CDC_ON_BOOT=1 -DARDUINO_USB_MODE=1
```

## The prior "tooling block" — resolved

The original project documented the pioarduino path as *tooling-blocked* (pioarduino
postinstall vs PlatformIO 6.1.19). With release **53.03.13** that block does **not**
recur on PlatformIO 6.1.19 — `pio run -e smoke` builds cleanly and flashes. The base
board def advertises "N8, No PSRAM", but `memory_type=qio_opi` + `BOARD_HAS_PSRAM`
correctly drive the N16R8; runtime confirms 8 MB.

## M0 hardware validation (examples/01_smoke_blink_serial)

Built + flashed to the device; USB-CDC serial came up and reported:
```
Arduino-ESP32 3.1.3 / IDF 5.3.x, chip ESP32-S3
flash 16 MB, PSRAM 8.0 MB
[smoke Ns] alive gpio18=1 PSRAM=8.0MB heap=350676 ...
```
→ toolchain builds, USB-CDC works, PSRAM=8 MB, GPIO toggles, ~350 KB internal heap.

## Still to re-validate on hardware (per milestone)

- **M2:** e-paper refresh times (~2.2 s / ~18.5 s), encoder events, SD mount + read-back,
  NVS+JSON round-trip. Confirm the NeoPixel RMT path on IDF5 has no per-show heap leak
  (the LED driver's ~60 FPS loop relies on it) — watch heap / `stackHighWaterBytes()`.
- **M3:** the i2s_std (TX) + i2s_pdm (RX) rewrite; validate TX and RX independently.
  Re-check `i2s_channel_read` into a PSRAM buffer (PSRAM DMA rules on IDF5). **Needs 5 V.**
- **M4:** speaker→mic acoustic loopback. **Needs 5 V.**

## Fallback (not needed, kept for the record)

If a future pioarduino bump breaks the build, pin back to `53.03.13`, or fall back to
stock `espressif32@7.0.1` (Arduino 2.0.17) and use the legacy `driver/i2s.h` audio
implementation — every other driver is framework-agnostic.
