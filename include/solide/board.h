#pragma once
#include <stdint.h>

// ============================================================================
// solide::Board — the device pin map as data.
//
// The Solide device is a FIXED hardware build, but the pinout is expressed as a
// constexpr struct (not bare #defines) so drivers read pins as data and a future
// board variant is a new Board constant + a new -DSOLIDE_BOARD value, with zero
// driver changes. board() returns the compile-time-selected board.
// ============================================================================

namespace solide {

// GPIO numbers; -1 = unused/not-connected.
struct Board {
  const char* name;
  struct { int8_t cs, mosi, sck, miso; }        sd;    // SPI (FSPI/SPI2)
  struct { int8_t sck, mosi, cs, dc, rst, busy; } epd;  // SPI (HSPI/SPI3), MISO unused
  struct { int8_t din; uint16_t count; }        led;   // WS2812B data + pixel count
  struct { int8_t a, b, sw; }                   enc;   // EC11 quadrature + switch
  struct { int8_t bclk, lrclk, din; }           spk;   // I2S TX (speaker amp)
  struct { int8_t bclk, ws, din; }              mic;   // I2S std RX (INMP441/ICS-43434)
};

// The active board, selected at build time via -DSOLIDE_BOARD=<id> (default: solide_s3).
const Board& board();

}  // namespace solide
