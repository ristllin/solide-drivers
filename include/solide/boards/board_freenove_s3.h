#pragma once
#include "solide/board.h"

// ============================================================================
// Pin map for the Freenove ESP32-S3 Display (FNK0104B) - an all-in-one board:
// ESP32-S3 N16R8 (16 MB flash, 8 MB PSRAM), 2.8" ILI9341 240x320 IPS panel,
// FT6336U capacitive touch, ES8311 audio codec (mic + speaker), on-board SDMMC
// card slot, a single WS2812 status LED, and a 1S Li-ion battery input.
//
// Sources (all authoritative, from Freenove/Freenove_ESP32_S3_Display):
//   Display SPI + backlight  Libraries/FNK0104AB/.../FNK0104AB_2.8_240x320_ILI9341.h
//   Touch / codec / SDMMC    Tutorial_With_Touch example sketches (AB #ifdef arm)
//   Schematic                Schematic/2.8inch_ESP32-S3_Display_Schematic.pdf
//
// PANEL: ILI9341, colour order BGR, inversion ON, 40 MHz SPI. The framebuffer is
// board-independent RGB565; BGR + inversion are applied by the panel init/MADCTL
// in display_tft (see audioKind/touchKind style branching), NOT by the renderer,
// so the golden_tft frames are reused unchanged.
//
// STRAPPING-PIN NOTE: TFT_DC = GPIO46 and TFT_BL = GPIO45 are ESP32-S3 strapping
// pins (ROM-msg / VDD_SPI). This is safe and is how Freenove ships the board:
// they are only sampled at reset and are driven as plain outputs afterwards.
// Do not add pull resistors or change their boot levels.
//
// I2C BUS: touch (FT6336U) and codec (ES8311) SHARE one I2C bus on SDA 16 / SCL
// 15. A single owner must call Wire.begin(16, 15) once; neither driver re-inits it.
// ============================================================================

namespace solide {

inline constexpr Board kBoardFreenoveS3 = {
  "freenove-s3 (FNK0104B 2.8\" ILI9341 CYD)",
  // No SPI card: the slot is SDMMC (see sdmmc{} below). sd{} left unfitted.
  /* sd  */ { /*cs*/ -1, /*mosi*/ -1, /*sck*/ -1, /*miso*/ -1 },
  // No e-paper on this board.
  /* epd */ { /*sck*/ -1, /*mosi*/ -1, /*cs*/ -1, /*dc*/ -1, /*rst*/ -1, /*busy*/ -1 },
  // ILI9341 on a dedicated SPI bus (10-13); DC 46 / BL 45 are strapping pins (OK).
  // RST -1: the panel reset is tied to the board EN/RST line. No SPI touch here
  // (tcs/tirq -1); touch is FT6336U over I2C (touchI2c{}).
  /* tft */ { /*sck*/ 12, /*mosi*/ 11, /*miso*/ 13, /*cs*/ 10, /*dc*/ 46, /*rst*/ -1,
              /*bl*/ 45, /*tcs*/ -1, /*tirq*/ -1 },
  // Single on-board WS2812 status pixel (NOT a ring). hasRing=false routes the
  // notifier ring to the panel instead.
  /* led */ { /*din*/ 42, /*count*/ 1 },
  // No rotary encoder (touch-only).
  /* enc */ { /*a*/ -1, /*b*/ -1, /*sw*/ -1 },
  // Audio is the ES8311 codec (codec{}), not discrete I2S mic/amp. spk/mic unfitted.
  /* spk */ { /*bclk*/ -1, /*lrclk*/ -1, /*din*/ -1 },
  /* mic */ { /*bclk*/ -1, /*ws*/ -1, /*din*/ -1 },
  // BAT+ -[R]- GPIO9 -[R]- GND (÷2); single Li-ion cell (3.7-4.2 V).
  /* batt */ { /*sense*/ 9, /*dividerX100*/ 200, /*cells*/ 1 },
  // ---- capabilities ----
  /* hasRing   */ false,                    // 1 status pixel; ring renders on panel
  /* touchKind */ TouchKind::CapacitiveI2c, // FT6336U (pixel coords; no min/max cal)
  /* audioKind */ AudioKind::Es8311Codec,
  /* sdKind    */ SdKind::Sdmmc,
  /* tftInvert */ true,                     // FNK0104AB ships with TFT_INVERSION_ON
  /* onboardRgb*/ -1,                       // status LED is the normal led.din (42)
  /* touchI2c  */ { /*sda*/ 16, /*scl*/ 15, /*intr*/ 17, /*rst*/ 18, /*addr*/ 0x38 }, // FT6336U
  // ES8311: MCLK 4, BCLK 5, WS 7, DOUT(to spk) 8, DIN(from mic) 6; I2C 16/15;
  // ampEn GPIO1, ACTIVE-LOW (Freenove FNK0104 `#define AP_ENABLE 1` + driver_es8311_init
  // drives it LOW to enable; without it the speaker amp stays off); addr 0x18.
  /* codec     */ { /*mclk*/ 4, /*bclk*/ 5, /*ws*/ 7, /*dout*/ 8, /*din*/ 6,
                    /*i2cSda*/ 16, /*i2cScl*/ 15, /*ampEn*/ 1, /*addr*/ 0x18,
                    /*ampActiveHigh*/ false },
  // SDMMC 4-bit.
  /* sdmmc     */ { /*clk*/ 38, /*cmd*/ 40, /*d0*/ 39, /*d1*/ 41, /*d2*/ 48, /*d3*/ 47 },
};

}  // namespace solide
