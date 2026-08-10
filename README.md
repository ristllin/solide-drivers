# solide-drivers

A reusable **board-support package** for the **Solide S3** device — an
ESP32-S3-DevKitC-1 **N16R8** (16 MB flash, 8 MB octal PSRAM) with an e-paper
display, a 45-LED WS2812B ring, an I2S speaker + I2S mic, an SD card, and an EC11
encoder. It extracts the proven drivers, hardware knowledge, and docs from the
original firmware into a clean, documented, tested library so any firmware can
program this hardware without reinventing the drivers.

**Hardware only** — no app/agent/network code. Import it, get the peripherals.

## Peripherals

| Namespace | Peripheral | Highlights |
|---|---|---|
| `solide::display` | WeAct 2.9" 3-colour e-paper (SSD1680) | fast 2-colour B/W (~2.2 s) + 3-colour (~18.5 s) |
| `solide::leds` | WS2812B ring ×45 | patterns + agent-status segmentation + colour schemes, ~60 FPS |
| `solide::audio` | I2S speaker + I2S mic | play/record, modern i2s_std (TX + RX) |
| `solide::storage` | SD card (FAT32) | graceful file I/O |
| `solide::memory` | NVS + SD | typed persistent settings/state |
| `solide::input` | EC11 encoder + button | quadrature decode + `solide::menu` FSM |

## Quick start

```ini
; platformio.ini of your firmware
[env:esp32s3]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.39/platform-espressif32.zip
board = esp32-s3-devkitc-1
framework = arduino
board_build.arduino.memory_type = qio_opi
board_upload.flash_size = 16MB
board_build.partitions = default_16MB.csv
build_flags = -DBOARD_HAS_PSRAM -DARDUINO_USB_CDC_ON_BOOT=1 -DARDUINO_USB_MODE=1
lib_deps = symlink://../solide-drivers   ; or a pinned git tag
```

```cpp
#include <solide/solide.h>
void setup() { solide::begin(); solide::leds::show(solide::leds::Pattern::Rainbow); }
```

See [`docs/`](docs/) for the hardware reference, per-peripheral guides, and the
serial `TEST` self-test protocol; [`AGENTS.md`](AGENTS.md) for the machine-readable
entry point; and [`examples/`](examples/) for a sketch per peripheral.

## Building & testing

```bash
pio test -e native                        # host unit tests (portable core)
pio run  -e smoke -t upload -t monitor    # platform smoke test on hardware
```

Toolchain: Arduino-ESP32 3.3.9 / ESP-IDF 5.5.4 via pioarduino 55.03.39 (see
[`docs/modernization.md`](docs/modernization.md)). MIT licensed.
