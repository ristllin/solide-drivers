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
// TWO DISPLAY VARIANTS share these pads, and a device is wired for exactly one:
//   epd — 2.9" SSD1680 e-paper + EC11 knob (default, the shipped build)
//   tft — 2.8" ILI9341 colour touchscreen; consumes the encoder pins (1/2/48),
//         so there is no knob. Selected by screenModel in firmware NVS.
// The spare list above applies to the e-paper build; those spares sit on the J1
// header, which the carrier does not break out, so they cannot rescue the TFT.
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
  // Colour TFT + touch variant (ILI9341 240x320 + XPT2046), fitted INSTEAD of
  // the e-paper. Only the J3 header is reachable on the carrier, and GPIO
  // 35/36/37 (OCTAL PSRAM) split it, so the seven display/touch signals occupy
  // the one contiguous usable run — which means the encoder pins are consumed:
  //   1 = MISO (was enc.a), 2 = backlight (was enc.b), 48 = touch CS (was enc.sw).
  // T_CLK/T_DIN/T_DO are bridged to SCK/SDI/SDO on the module, so display and
  // touch are one bus with two chip selects. T_IRQ is left unconnected (poll).
  /* tft */ { /*sck*/ 42, /*mosi*/ 41, /*miso*/ 1, /*cs*/ 38, /*dc*/ 40, /*rst*/ 39,
              /*bl*/ 2, /*tcs*/ 48, /*tirq*/ -1 },
  /* led */ { /*din*/ 21, /*count*/ 45 },
  /* enc */ { /*a*/ 1, /*b*/ 2, /*sw*/ 48 },
  /* spk */ { /*bclk*/ 7, /*lrclk*/ 8, /*din*/ 17 },
  /* mic */ { /*bclk*/ 15, /*ws*/ 18, /*din*/ 16 },   // I2S std: INMP441/ICS-43434 (SCK=15, WS=18, SD=16, L/R->GND)
  // BAT+ -[220k]- GPIO4 -[100k]- GND (÷3.2), tapped before the DC-DC; 2S pack.
  /* batt */ { /*sense*/ 4, /*dividerX100*/ 320, /*cells*/ 2 },
};

}  // namespace solide
