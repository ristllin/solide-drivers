#pragma once
#include "solide/board.h"

// ============================================================================
// Canonical pin map for the Solide S3 device — ESP32-S3-DevKitC-1 N16R8
// (16 MB QIO flash, 8 MB OCTAL PSRAM). Source of truth; also mirrored in
// docs/hardware.md and docs/manifest.json (generated from this file).
//
// RESERVED GPIOs — never assign: 0/45/46 (strapping), 19/20 (native USB),
// 43/44 (UART0), 26-32 (flash), 33-37 (OCTAL PSRAM — the N16R8 gotcha),
// 48 (onboard RGB, repurposed here as the encoder switch). Free spares: 3,4,5,6,9
// (GPIO 18 is now the I2S mic WS/LRCLK).
//
// Power: SD + e-paper + audio run at 3.3 V; the LED ring needs the 5 V bus to
// light and the audio amp uses it for volume. The audio board's shared VCC is
// 3.3 V ONLY — the PDM mic DATA line follows VCC and 5 V would damage the S3.
// ============================================================================

namespace solide {

inline constexpr Board kBoardSolideS3 = {
  "solide-s3 (ESP32-S3-DevKitC-1 N16R8)",
  /* sd  */ { /*cs*/ 10, /*mosi*/ 11, /*sck*/ 12, /*miso*/ 13 },
  /* epd */ { /*sck*/ 38, /*mosi*/ 39, /*cs*/ 40, /*dc*/ 41, /*rst*/ 42, /*busy*/ 47 },
  /* led */ { /*din*/ 21, /*count*/ 45 },
  /* enc */ { /*a*/ 1, /*b*/ 2, /*sw*/ 48 },
  /* spk */ { /*bclk*/ 7, /*lrclk*/ 8, /*din*/ 17 },
  /* mic */ { /*bclk*/ 15, /*ws*/ 18, /*din*/ 16 },   // I2S std: INMP441/ICS-43434 (SCK=15, WS=18, SD=16, L/R->GND)
};

}  // namespace solide
