# Hardware reference - Solide S3

The canonical, machine-readable version is [`manifest.json`](manifest.json) (generated
from `include/solide/boards/board_solide_s3.h`, the pin source of truth). This page is
the human reference; **[build.md](build.md)** is the module-by-module wiring +
power + BOM assembly guide (with a [diagram](wiring.svg)).

## Board

**ESP32-S3-DevKitC-1 N16R8** - dual-core Xtensa LX7 @ 240 MHz + FPU, **16 MB** QIO
flash, **8 MB OCTAL (OPI) PSRAM**.

### Reserved GPIOs - never assign
| Pins | Use |
|---|---|
| 0, 45, 46 | strapping |
| 19, 20 | native USB (D−/D+) |
| 43, 44 | UART0 console |
| 26–32 | SPI flash |
| **33–37** | **OCTAL PSRAM - the N16R8 gotcha** |
| 48 | on-board RGB (repurposed here as the encoder switch) |

Free spares: **3, 4, 5, 6, 9**. (GPIO 18 is now the I2S mic WS/LRCLK - no longer spare.)

## Pin map

| Peripheral | Bus | Pins | Power | Notes |
|---|---|---|---|---|
| **SD card** | FSPI/SPI2 | CS 10 · MOSI 11 · SCK 12 · MISO 13 | 3.3 V | FAT32; native IOMUX (fast) |
| **E-paper** WeAct 2.9" 3-colour (SSD1680) | HSPI/SPI3 | SCK 38 · MOSI 39 · CS 40 · DC 41 · RST 42 · BUSY 47 | 3.3 V | 4 MHz; MISO unused. WeAct labels SPI I2C-style: SDA=MOSI, SCL=SCK |
| **LED ring** WS2812B ×45 | RMT | DIN 21 | **5 V** power, 3.3 V logic | ~372 mA worst case @ brightness 30 |
| **Encoder** EC11 | GPIO | A 1 · B 2 · SW 48 | 3.3 V | 3-pin side: A / common→GND / B. 2-pin side: switch |
| **Speaker** I2S amp (MAX98357A) | I2S1 (std) | BCLK 7 · LRCLK 8 · DIN 17 | 3.3 V | Class-D, built-in thermal/over-current protection; amp needs the 5 V bus for volume |
| **Mic** I2S MEMS (INMP441/ICS-43434) | I2S0 (std) | BCLK 15 · WS 18 · SD 16 | **3.3 V only ⚠** | 16 kHz/16-bit/mono. L/R→GND (left slot). VDD/data follow VCC - 5 V damages the S3 |

## Power

`USB-C 2S BMS → 2×18650 (series) → DC-DC → 5 V bus`.

| Rail | Powers | Always on? |
|---|---|---|
| **3.3 V** | ESP32-S3, e-paper, SD, audio (amp + mic) | yes (USB/regulator) |
| **5 V bus** | LED ring, audio-amp volume headroom | switchable |

So the ESP32 + e-paper + SD + encoder + mic all work on USB alone; the **LED ring
won't light and the speaker won't drive audibly without the 5 V bus**.

## Refresh / timing
- E-paper fast B/W: **~2.2 s** (`fastFull ≈ 2 212 000 µs`), via the custom WS_20_30 LUT.
- E-paper 3-colour: **~18.5 s** (OTP waveform) - reserve for idle/art.
- LED ring: ~60 FPS render loop; no per-show RMT heap leak on IDF5.

## Battery sense (add-on)
The device has no built-in battery gauge. To read pack charge, add a divider from
the **2S pack** (6.0–8.4 V, *before* the DC-DC - the regulated 5 V stays flat and
tells you nothing) into a free ADC1 pin:

`BAT+ → 220 kΩ → node → 100 kΩ → GND`, and `node → GPIO 4 (ADC1)` (÷3.2, 8.4 V→~2.6 V).
Read `analogReadMilliVolts(4) × 3.2` = pack volts → rough SoC.
Full wiring + code pointer in [build.md](build.md#battery-sense-add-on); a
`solide::power` driver is the planned home once it's wired.

## Caveats worth repeating
- **Audio VCC is 3.3 V only.** The mic VDD/data lines follow VCC; 5 V will damage
  the S3's input.
- **Battery sense reads the pack, not the 5 V rail** (the DC-DC output is regulated flat).
- **Octal PSRAM occupies GPIO 33–37.** Never route a peripheral there.
- The base board def (`esp32-s3-devkitc-1`) advertises "N8, no PSRAM"; the N16R8 is
  driven by `board_build.arduino.memory_type=qio_opi` + `-DBOARD_HAS_PSRAM` (see
  `platformio.ini`). Runtime confirms 8 MB.
