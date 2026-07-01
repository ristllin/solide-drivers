#pragma once

// Umbrella header — one include for the whole board-support package.
#include "solide/board.h"
#include "solide/display.h"
#include "solide/leds.h"
#include "solide/storage.h"
#include "solide/memory.h"
#include "solide/input.h"
#include "solide/menu.h"
#include "solide/selftest.h"
#include "solide/status_art.h"

namespace solide {

struct BeginResult { bool display, leds, storage, memory, input; };

// Bring up all peripherals. Each is independent — a failure of one does not stop
// the others; the returned mask reports which came up. Consumers that don't have
// (or don't want) a given peripheral can call the per-peripheral begin()s instead.
BeginResult begin();

}  // namespace solide
