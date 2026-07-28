#include "solide/solide.h"

namespace solide {

BeginResult begin(const BeginOptions& opt) {
  BeginResult r{};
  r.storage = storage::begin();   // mount SD first (memory's JSON half uses it)
  r.memory  = memory::begin();
  if (opt.tft) {
    // The TFT and the e-paper share the SPI3 pads, and the TFT's pins take over
    // the encoder's — so a board fitted with one must NOT initialise the other.
    // Touch borrows the panel's bus, hence the ordering.
    r.display = display_tft::begin();
    r.touch   = r.display && touch::begin();
    r.input   = false;              // no knob on a TFT board, by construction
  } else {
    r.display = display::begin();
    r.input   = input::begin();
  }
  r.leds    = leds::begin();
  r.battery = battery::begin();  // false = divider not fitted (benign)
  return r;
}

}  // namespace solide
