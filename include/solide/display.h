#pragma once
#include <Arduino.h>
#include "solide/menu.h"   // solide::menu::MenuView

// ============================================================================
// E-paper display - WeAct 2.9" 3-colour (SSD1680). All GxEPD2/SPI access runs
// inside a dedicated render task (the 3-colour panel takes ~15 s per full
// refresh), so callers only enqueue requests and never block.
//
// Two refresh paths on one panel: fast B/W (custom WS_20_30 LUT, ~2.2 s) for
// interactive UI, and 3-colour (OTP waveform, ~18.5 s) for stable/idle art.
//
// This is a HARDWARE driver - it holds no app/branding/network concepts. Compose
// your own status screen from requestText() / requestBitmap() (see examples/).
// ============================================================================
namespace solide::display {

bool begin();       // init panel (dedicated HSPI) + start render task; false on failure
bool taskAlive();   // true once the render task is running (self-test)

// A titled text block - fast B/W (~2.2 s): bold `title` (up to 2 lines) + wrapped
// `body`. With scrollMode, the body is knob-scrollable with a "[ click: return ]"
// footer; scrollOffset is the first body line to show. Non-ASCII bytes are dropped
// (the GFX font is ASCII-only). Use it for prompts, replies, any text screen.
void requestText(const String& title, const String& body,
                 int scrollOffset = 0, bool scrollMode = false);
int  maxTextScroll();   // max scroll offset of the last text block (to clamp the encoder)

// A knob menu (solide::menu::MenuView). Fast partial refresh (~0.75 s) unless `full`.
void requestMenu(const solide::menu::MenuView& view, bool full = false);

// Full-screen 1-bit bitmap(s): the `black` plane draws black; the `red` plane
// draws red in 3-colour (fast=false) or merges to black in fast B/W (fast=true).
// Either pointer may be null. Data must stay valid (PROGMEM / static).
// fullClear (fast B/W only): render this frame with the panel's TRUE full-update
// waveform (slower, but the only path that WIPES accumulated ghosting) instead of
// the fast custom LUT. Used periodically (FullRefreshEveryN) + on a long-idle screen.
// partial (fast B/W only): the SSD1680's differential mode - refreshes with NO
// invert flash (the same flicker-free path requestMenu uses; ~0.75 s). Every 10th
// partial internally falls back to a fast full frame to bound ghost accumulation.
// Ignored when fullClear is set.
void requestBitmap(const uint8_t* black, const uint8_t* red,
                   int16_t w, int16_t h, bool fast, bool fullClear = false,
                   bool partial = false);

// Device status mascot (solide::art::State) - a convenience over requestBitmap.
void showArt(int state, bool fast);

// Blank the panel (fast B/W white).
void clear();

}  // namespace solide::display
