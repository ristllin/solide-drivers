#pragma once
#include <Arduino.h>

// ============================================================================
// Resistive touch — XPT2046, the input device on a TFT-fitted board (it takes
// the place of the EC11 encoder, whose pins the panel consumes).
//
// It shares the display's SPI bus with its own chip select, and is clocked far
// slower than the panel (the controller tops out around 2 MHz), so every read
// runs in its own transaction at its own speed.
//
// This driver reports DEBOUNCED, CALIBRATED PANEL COORDINATES — raw ADC counts
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

// Raw, UNCALIBRATED ADC counts — for the calibration routine and the console
// diagnostic only. Returns false if nothing is touching.
bool readRaw(uint16_t& x, uint16_t& y, uint16_t& z);

// Calibration: maps raw ADC counts to panel pixels. Defaults are the typical
// values for these modules; a device can persist its own after running the
// calibration screen. minX/minY correspond to panel (0,0).
struct Calibration {
  uint16_t minX = 200, maxX = 3900;
  uint16_t minY = 200, maxY = 3900;
  bool swapXY = false;    // panel wired portrait vs the controller's axes
  bool invertX = false;
  bool invertY = false;
};
void setCalibration(const Calibration& c);
Calibration calibration();

}  // namespace solide::touch
