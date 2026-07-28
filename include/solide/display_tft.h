#pragma once
#include <Arduino.h>
#include <SPI.h>

// ============================================================================
// Colour TFT display — 2.8" ILI9341 240x320, the ALTERNATIVE to the e-paper
// panel (solide/display.h). A device is fitted with one or the other; the
// firmware picks which driver binds at boot.
//
// Unlike the e-paper driver this holds no drawing primitives: the caller
// composes a finished RGB565 framebuffer and pushes it. That keeps every
// layout decision in portable, host-testable code and keeps this file to the
// three things that genuinely need hardware — the init sequence, the blit, and
// the backlight.
//
// Pixel format: RGB565, BIG-ENDIAN (high byte first), row-major, 240x320
// portrait. That is the ILI9341's own wire order, so a frame is written
// straight out with no per-pixel swapping.
//
// The blit runs on a dedicated render task: a full frame is ~150 KB and about
// 31 ms of SPI at 40 MHz, which is far too long to sit in the main loop. Push
// is therefore asynchronous — pushFrame() hands the buffer to the task and
// returns. The BUFFER MUST STAY VALID AND UNMODIFIED until busy() goes false
// (the caller owns it; see hw/tft_out.cpp, which double-buffers in PSRAM).
// ============================================================================
namespace solide::display_tft {

constexpr int16_t kW = 240;
constexpr int16_t kH = 320;

bool begin();      // init panel + backlight + start the render task; false on failure
bool taskAlive();  // true once the render task is running (self-test)

// Queue a full-screen RGB565 (big-endian) frame. Returns false if the driver
// is not up or a previous frame is still in flight — the caller should keep its
// buffer and retry, never free or overwrite it.
bool pushFrame(const uint16_t* fb);
bool busy();       // a frame is currently being written to the panel

// Backlight, 0-100 %. 0 blanks the panel without losing its contents, which is
// what the idle/screensaver path uses: on a TFT the backlight IS the idle draw,
// so blanking it is the real power saving (an e-paper screensaver image is not).
void setBacklight(uint8_t pct);
uint8_t backlight();

void fill(uint16_t colour565);  // solid fill (bring-up + clear)
void clear();                   // fill(0) — black

// The SPI bus the panel owns. The touch controller is bridged onto the same
// SCK/MOSI/MISO on the module and shares it with a separate chip select, so it
// borrows the bus through here rather than binding the pins a second time.
// Callers MUST wrap their access in beginTransaction/endTransaction with their
// own SPISettings — the touch controller cannot survive the panel's 40 MHz.
// The pointer is always valid, but the bus is only USABLE after begin().
SPIClass* bus();

}  // namespace solide::display_tft
