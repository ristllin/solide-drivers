#pragma once

// Umbrella header — one include for the whole board-support package.
#include "solide/board.h"
#include "solide/display.h"
#include "solide/display_tft.h"
#include "solide/touch.h"
#include "solide/leds.h"
#include "solide/storage.h"
#include "solide/memory.h"
#include "solide/input.h"
#include "solide/battery.h"
#include "solide/menu.h"
#include "solide/selftest.h"
#include "solide/status_art.h"

namespace solide {

struct BeginResult { bool display, leds, storage, memory, input, battery, touch; };

// Which display/input pair to bring up. A board is fitted with EITHER the
// e-paper + EC11 knob OR the colour TFT + touch — they share the SPI3 pads and
// the TFT consumes the encoder GPIOs, so bringing up both would have two
// drivers configuring the same pins. Defaults to the e-paper pair, which is
// the shipped build and what every existing caller expects.
struct BeginOptions {
  bool tft = false;   // true: colour TFT + touch instead of e-paper + encoder
};

// Bring up all peripherals. Each is independent — a failure of one does not stop
// the others; the returned mask reports which came up. Consumers that don't have
// (or don't want) a given peripheral can call the per-peripheral begin()s instead.
BeginResult begin(const BeginOptions& opt = {});

}  // namespace solide
