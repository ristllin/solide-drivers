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
    r.leds    = leds::begin();
    r.touch   = r.display && touch::begin();
    r.input   = false;              // no knob on a TFT board, by construction
  } else {
    // ⚠ Keep this branch in the ORIGINAL order (display -> leds -> input). It is
    // the shipped path, so it must remain a literal no-op against the previous
    // release — hoisting leds() past input() would change which subsystem claims
    // heap first at boot and start the 1 kHz encoder task before the ring's RMT
    // init, for no reason this feature needs.
    r.display = display::begin();
    r.leds    = leds::begin();
    r.input   = input::begin();
  }
  r.battery = battery::begin();  // false = divider not fitted (benign)
  return r;
}

}  // namespace solide
