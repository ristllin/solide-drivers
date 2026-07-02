# Getting started

## Import into your firmware

```ini
; platformio.ini
[env:esp32s3]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.39/platform-espressif32.zip
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
board_build.arduino.memory_type = qio_opi
board_upload.flash_size = 16MB
board_build.partitions = default_16MB.csv
build_flags = -DBOARD_HAS_PSRAM -DARDUINO_USB_CDC_ON_BOOT=1 -DARDUINO_USB_MODE=1 -DSOLIDE_BOARD=solide_s3
lib_deps =
    symlink://../solide-drivers          ; or: https://github.com/ristllin/solide-drivers.git#v0.1.0
```
The package's own dependencies (Adafruit NeoPixel, GxEPD2, ArduinoJson) resolve
transitively — you don't restate them.

## Minimal sketch

```cpp
#include <solide/solide.h>
using namespace solide;

void setup() {
  Serial.begin(115200);
  begin();                                   // brings up every peripheral present
  leds::show(leds::Pattern::Rainbow);        // (lights with the 5 V bus)
  display::requestText("Hello", "solide-drivers is up.");
}

void loop() {
  input::Event e;
  while (input::pop(e)) { /* RotateCW/CCW, Click, LongPress */ }
}
```

You can also bring peripherals up individually (`display::begin()`, `leds::begin()`,
`storage::begin()`, `memory::begin()`, `input::begin()`, `audio::begin()`) — each
returns `bool` and is safe to skip if you don't use it.

## Build / flash / test

```bash
pio test -e native                                          # host unit tests (no hardware)
SOLIDE_EXAMPLE=99_combined_demo pio run -e esp32s3 -t upload # flash an example
```
Before any upload, free the serial port: `lsof -t /dev/cu.usbmodem* | xargs kill`.

See [`../examples/`](../examples) for one sketch per peripheral, [`hardware.md`](hardware.md)
for the pin map, and [`testing.md`](testing.md) for the on-device self-test protocol.
