#pragma once
#include <Arduino.h>

// ============================================================================
// Resistive touch - XPT2046, the input device on a TFT-fitted board (it takes
// the place of the EC11 encoder, whose pins the panel consumes).
//
// It shares the display's SPI bus with its own chip select, and is clocked far
// slower than the panel (the controller tops out around 2 MHz), so every read
// runs in its own transaction at its own speed.
//
// This driver reports DEBOUNCED, CALIBRATED PANEL COORDINATES - raw ADC counts
// and the pressure threshold stay in here. Callers get {x, y, down} in the same
// 240x320 space the framebuffer uses, so hit-testing is a plain rectangle
// compare. Coordinates are only meaningful while down is true.
// ============================================================================
namespace solide::touch {

struct Point {
  int16_t x = -1;      // panel px, 0..239 (-1 when not down)
  int16_t y = -1;      // panel px, 0..319 (-1 when not down)
  bool    down = false;
  uint16_t pressure = 0;  // raw Z; 0 when not down (diagnostics/calibration)
};

bool begin();      // configure the chip select + prime the controller
bool present();    // true once begin() succeeded

// Current debounced state. Cheap enough to call every loop; it rate-limits
// its own SPI reads internally.
Point read();

// Raw, UNCALIBRATED ADC counts - for the calibration routine and the console
// diagnostic only. Returns false if nothing is touching.
bool readRaw(uint16_t& x, uint16_t& y, uint16_t& z);

// Calibration: maps raw ADC counts to panel pixels. Defaults are the typical
// values for these modules; a device can persist its own after running the
// calibration screen. minX/minY correspond to panel (0,0).
struct Calibration {
  uint16_t minX = 200, maxX = 3900;
  uint16_t minY = 200, maxY = 3900;
  // ⚠ swapXY DEFAULTS TRUE because the panel is mounted LANDSCAPE. The XPT2046's
  // axes are fixed to the glass, so they follow the module's NATIVE portrait
  // orientation - but MADCTL rotates the display 90 degrees, which means screen X
  // now runs along the controller's Y. Rotating the display without rotating this
  // mapping puts every tap on the wrong axis, which is indistinguishable from
  // "touch is dead" and is exactly how it was first reported.
  // Must stay in step with display_tft's MADCTL and nimbus/tft_render/theme.h.
  bool swapXY = true;
  bool invertX = false;
  bool invertY = true;    // landscape: controller Y increases opposite screen X
};
void setCalibration(const Calibration& c);
Calibration calibration();

}  // namespace solide::touch
