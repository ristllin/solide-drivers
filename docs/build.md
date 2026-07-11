# Build & wiring guide — Solide S3

How to physically assemble the device this package drives. Pin numbers are the
canonical ones from `include/solide/boards/board_solide_s3.h` (also in
[`manifest.json`](manifest.json)); the [pin reference](hardware.md) has the tables,
this page has the **module-by-module wiring + power + assembly**. Overview diagram:
[`wiring.svg`](wiring.svg).

## Bill of materials

| # | Part | Notes |
|---|---|---|
| 1 | **ESP32-S3-DevKitC-1 N16R8** | 16 MB flash / 8 MB octal PSRAM. The brain. |
| 1 | **WeAct 2.9" 3-colour e-paper** (SSD1680) | B/W + red, 296×128 |
| 1 | **WS2812B LED ring, 45 px** | addressable RGB |
| 1 | **MAX98357A I2S amp** | I2S Class-D amp (built-in thermal + over-current protection) |
| 1 | **INMP441 / ICS-43434 I2S MEMS mic** | I2S-std MEMS microphone (separate breakout) |
| 1 | **EC11 rotary encoder** with push switch | 3-pin + 2-pin |
| 1 | **microSD module** (SPI) + FAT32 card | |
| 2 | **18650 Li-ion cells** | wired in **series** = 2S |
| 1 | **2S BMS with USB-C charge** | protection + balance + charging |
| 1 | **DC-DC converter → 5 V** | from the 2S pack (~6–8.4 V) to a clean 5 V bus |
| — | **battery-sense add-on:** 1× 220 kΩ, 1× 100 kΩ, 1× 100 nF | see [§ Battery sense](#battery-sense-add-on) |
| — | wire, protoboard/PCB, JST connectors | |

## Power architecture

```
USB-C ─▶ 2S BMS ─▶ 2×18650 (series, ~6.0–8.4 V) ─▶ DC-DC ─▶ 5 V bus
                                                              │
        5 V bus ─┬─▶ LED ring (+5V)                           │
                 ├─▶ ESP32-S3 VIN/5V ─▶ (on-board LDO) ─▶ 3.3 V rail
                 └─▶ (optional louder amp, see audio note)
        3.3 V rail ─▶ e-paper · microSD · mic + amp VCC · encoder pull-ups
```

- **On battery:** the DC-DC 5 V bus feeds the ESP32's VIN; the DevKit's on-board
  regulator makes the 3.3 V rail. USB-C on the BMS charges the pack.
- **On USB (dev):** the DevKit runs from USB; the 5 V bus is only needed to light
  the ring / drive the amp.
- **Every ground is common** — tie all GNDs together (cells, BMS, DC-DC, ESP32,
  every module).

> ⚠️ **The mic VCC is 3.3 V ONLY.** The mic VDD/data lines follow VCC, and
> 5 V would damage the S3's input. Do **not** put the mic on the 5 V bus.

## Wiring — module by module

ESP32 pins below are GPIO numbers. `3V3`/`5V`/`GND` are the power rails above.

### E-paper (WeAct 2.9" 3-colour)
WeAct silk is I²C-style but it is **SPI** (SDA=data, SCL=clock).

| Module pin | → | ESP32 |
|---|---|---|
| VCC | → | **3V3** |
| GND | → | **GND** |
| SDA (MOSI) | → | GPIO **39** |
| SCL (SCK)  | → | GPIO **38** |
| CS  | → | GPIO **40** |
| D/C | → | GPIO **41** |
| RES (RST) | → | GPIO **42** |
| BUSY | → | GPIO **47** |

### microSD module (SPI)
| Module pin | → | ESP32 |
|---|---|---|
| VCC (3V3) | → | **3V3** |
| GND | → | **GND** |
| CS | → | GPIO **10** |
| MOSI (DI) | → | GPIO **11** |
| SCK (CLK) | → | GPIO **12** |
| MISO (DO) | → | GPIO **13** |

### WS2812B LED ring
| Module pin | → | ESP32 / rail |
|---|---|---|
| +5V | → | **5V bus** |
| GND | → | **GND** (common) |
| DIN | → | GPIO **21** (a 330 Ω series resistor on DIN is good practice) |

### EC11 rotary encoder
3-pin side = rotation (A · common · B); 2-pin side = the push switch.

| Encoder pin | → | ESP32 / rail |
|---|---|---|
| A (3-pin) | → | GPIO **1** |
| C / common (3-pin, middle) | → | **GND** |
| B (3-pin) | → | GPIO **2** |
| SW (2-pin) | → | GPIO **48** |
| SW (2-pin, other) | → | **GND** |

(Internal pull-ups are enabled in the driver — no external resistors needed.)

### Audio — MAX98357A I2S amp + INMP441/ICS-43434 I2S mic
Two separate breakouts. The **mic VCC is 3.3 V only ⚠** (5 V damages the S3); the
amp runs on 3.3 V as a status speaker (5 V bus for more volume).

| Module | Pin | Role | → | ESP32 / rail |
|---|---|---|---|---|
| amp | VCC | power | → | **3V3** (5V bus for louder) |
| amp | GND | ground | → | **GND** |
| amp | BCLK | bit clock | → | GPIO **7** |
| amp | LRCLK | word clock | → | GPIO **8** |
| amp | DIN | data in | → | GPIO **17** |
| mic | VDD | power | → | **3V3** ⚠ (never 5V) |
| mic | GND | ground | → | **GND** |
| mic | BCLK / SCK | bit clock | → | GPIO **15** |
| mic | WS / LRCLK | word select | → | GPIO **18** |
| mic | SD | data out | → | GPIO **16** |
| mic | L/R | channel select | → | **GND** (left slot) |

> The amp runs at reduced volume on 3.3 V (fine as a status speaker). If you need
> more SPL, use a **separate** 5 V amp that does **not** share VCC with the mic.

## Battery sense (add-on)

The DevKit has no battery gauge and a basic 2S BMS gives no telemetry, so add a
divider from the **pack** (not the regulated 5 V, which stays flat) into a free
ADC1 pin. The 2S pack (6.0–8.4 V) is scaled to the ADC's ~3.3 V range:

```
BAT+ (2S pack +, before the DC-DC)
   │
  [ R1 220kΩ ]
   │
   ├───────────────▶ GPIO 4  (ADC1)      ── optional 100 nF from node to GND
   │
  [ R2 100kΩ ]
   │
  GND
```
- **ADC pin:** GPIO **4** (free ADC1; GPIO 5 or 6 also work — avoid GPIO 18, now the
  I2S mic WS and also an ADC2 pin that clashes with WiFi).
- **Divider:** 220 k / 100 k → ÷3.2, so 8.4 V → ~2.6 V. Quiescent draw ≈ 26 µA.
- **Read:** `analogReadMilliVolts(4) × 3.2` = pack volts; map to a rough SoC
  (~8.4 V≈100 %, ~7.4 V≈50 %, ~6.4 V≈0 % — the Li-ion curve is flat mid-range and
  sags under load, so treat it as approximate). When the BMS trips on over-discharge
  the pack disconnects and you read 0.
- A `solide::power` driver (`power::begin(4, 220, 100)`, `power::packVolts()`,
  `power::percent()`) is the planned home for this once the divider is wired.

## Assembly gotchas

- **GPIO 33–37 are octal PSRAM** on the N16R8 — never route anything there. Also
  avoid 0/45/46 (strapping), 19/20 (USB), 43/44 (UART), 26–32 (flash).
- **Common ground everywhere**, including the LED ring and the cells.
- **Two separate SPI buses:** SD on FSPI (10/11/12/13), e-paper on HSPI
  (38/39/40/41/42/47) — don't merge them.
- **LED logic is 3.3 V** into a 5 V-powered ring; it works in practice, but a 330 Ω
  series resistor on DIN (and a 1000 µF cap across the ring's 5 V/GND) improves
  reliability on long runs.
- Format the microSD as **FAT32**.

## Verify the build

Flash the self-test console and drive it over serial (no 5 V needed for most):
```bash
SOLIDE_EXAMPLE=08_selftest_console pio run -e esp32s3 -t upload
# then over serial:  TEST all   ->   RESULT ... PASS/SKIP per peripheral
```
`led`/`epd`/`sd`/`memory`/`input` should PASS; `sd` SKIPs with no card; `audio`
needs the 5 V amp + a working mic. See [testing.md](testing.md).
