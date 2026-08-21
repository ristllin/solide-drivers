#pragma once
#include <stdint.h>

// ============================================================================
// solide::Board - the device pin map as data.
//
// The Solide device is a FIXED hardware build, but the pinout is expressed as a
// constexpr struct (not bare #defines) so drivers read pins as data and a future
// board variant is a new Board constant + a new -DSOLIDE_BOARD value, with zero
// driver changes. board() returns the compile-time-selected board.
// ============================================================================

namespace solide {

// Peripheral variants a board can carry. The default (value 0) matches the
// original Solide S3 so a board constant that omits these fields still describes
// that board; every board constant nonetheless sets them explicitly.
enum class TouchKind : uint8_t { None = 0, ResistiveSpi = 1, CapacitiveI2c = 2 };
enum class AudioKind : uint8_t { RawI2s = 0, Es8311Codec = 1 };
enum class SdKind    : uint8_t { Spi = 0, Sdmmc = 1 };

// GPIO numbers; -1 = unused/not-connected.
struct Board {
  const char* name;
  struct { int8_t cs, mosi, sck, miso; }        sd;    // SPI (FSPI/SPI2)
  struct { int8_t sck, mosi, cs, dc, rst, busy; } epd;  // SPI (HSPI/SPI3), MISO unused
  // Optional colour TFT + resistive touch (ILI9341 + XPT2046), an ALTERNATIVE
  // to the e-paper on the same SPI3 pads. Display and touch share sck/mosi/miso
  // and are separated by their own chip selects; the panel needs miso only
  // because the touch controller reports coordinates on it. bl = backlight
  // (PWM-capable; -1 = tied to 3V3, always on). tirq = pen-down interrupt
  // (-1 = poll). Fitting the TFT RELEASES the encoder pins - a device is wired
  // for one or the other, and screenModel in firmware NVS selects which driver
  // binds at boot. sck = -1 when the TFT isn't fitted.
  struct { int8_t sck, mosi, miso, cs, dc, rst, bl, tcs, tirq; } tft;
  struct { int8_t din; uint16_t count; }        led;   // WS2812B data + pixel count
  struct { int8_t a, b, sw; }                   enc;   // EC11 quadrature + switch
  struct { int8_t bclk, lrclk, din; }           spk;   // I2S TX (speaker amp)
  struct { int8_t bclk, ws, din; }              mic;   // I2S std RX (INMP441/ICS-43434)
  // Battery voltage sense: an ADC1 pin fed by a resistor divider from BAT+
  // (tapped BEFORE the DC-DC). dividerX100 = (Rtop+Rbot)/Rbot × 100 (320 = ÷3.2);
  // cells = series Li-ion count (pack mV ÷ cells = per-cell mV). sense = -1
  // when the divider isn't fitted - battery::begin() then reports absent.
  struct { int8_t sense; uint16_t dividerX100; uint8_t cells; } batt;

  // ---- Variant capabilities (drivers branch on these; all set explicitly) ----
  // hasRing: true when led{} drives a physical addressable ring the notifier
  //   animates. false for an all-in-one board whose led is a single status pixel;
  //   the notifier then renders the ring on the panel instead (see ring_out).
  bool      hasRing;
  TouchKind touchKind;   // which touch controller/bus binds when a panel is fitted
  AudioKind audioKind;   // raw dual-I2S mic+amp, or a single I2S codec (ES8311)
  SdKind    sdKind;      // SPI card, or on-board SDMMC (SDIO)
  bool      tftInvert;   // ILI9341 needs software inversion ON (ILI9341_2 modules)
  // DevKitC onboard WS2812 that must be blanked once at boot (it can latch
  // glowing when its pin is later repurposed). -1 = none / not repurposed.
  int8_t    onboardRgbPin;

  // Capacitive touch (FT6336U / GT911 class) over I2C. Read only when
  // touchKind == CapacitiveI2c; reports already-scaled pixel coordinates, so the
  // resistive min/max calibration does not apply (swap/invert flags only).
  struct { int8_t sda, scl, intr, rst; uint8_t addr; } touchI2c;

  // ES8311 mono codec: I2C control + a single full-duplex I2S port with MCLK.
  // Read only when audioKind == Es8311Codec. ampEn = power-amp enable GPIO
  // (-1 = always on / not fitted); ampActiveHigh selects the level that turns the
  // amp ON (the Freenove PA is active-LOW: driven LOW = enabled). Shares its I2C
  // bus with touchI2c when both are on the same SDA/SCL.
  struct { int8_t mclk, bclk, ws, dout, din, i2cSda, i2cScl, ampEn; uint8_t addr;
           bool ampActiveHigh; } codec;

  // On-board SDMMC (SDIO) card. Read only when sdKind == Sdmmc. d1..d3 = -1
  // selects 1-bit mode.
  struct { int8_t clk, cmd, d0, d1, d2, d3; } sdmmc;
};

// The active board, selected at build time via -DSOLIDE_BOARD=<id> (default: solide_s3).
const Board& board();

}  // namespace solide
