#include "solide/solide.h"

namespace solide {

BeginResult begin(const BeginOptions& opt) {
  BeginResult r{};
  // The DevKitC-1 module's onboard addressable (WS2812-class) RGB LED lives on
  // GPIO48 - the SAME physical pin this board repurposes as the touch
  // controller's chip select (TFT boards) or the encoder button (e-paper
  // boards). It was never told to go dark: nothing in this driver or in Nimbus
  // ever sent it an off data frame, and on the TFT variant the CS role then
  // holds the pin in a near-continuous strong HIGH - the known failure mode
  // that latches an addressable die glowing. A plain digitalWrite(LOW) would
  // not reliably clear it either; only a valid zero-brightness frame does.
  //
  // This MUST run first, before touch::begin()/input::begin() claim the pin
  // for their own role below - and it must never run again after that point.
  // Re-issuing the LED protocol later would fight whichever role now owns
  // GPIO48 (it would collide with in-flight touch SPI transactions on a TFT
  // board, or override the button's INPUT_PULLUP on an e-paper board), so
  // there is no safe way to offer a live "turn the LED back on" toggle on this
  // hardware - only a one-shot, boot-time clear.
  neopixelWrite(RGB_BUILTIN, 0, 0, 0);
  r.storage = storage::begin();   // mount SD first (memory's JSON half uses it)
  r.memory  = memory::begin();
  if (opt.tft) {
    // The TFT and the e-paper share the SPI3 pads, and the TFT's pins take over
    // the encoder's - so a board fitted with one must NOT initialise the other.
    // Touch borrows the panel's bus, hence the ordering.
    r.display = display_tft::begin();
    r.leds    = leds::begin();
    r.touch   = r.display && touch::begin();
    r.input   = false;              // no knob on a TFT board, by construction
  } else {
    // ⚠ Keep this branch in the ORIGINAL order (display -> leds -> input). It is
    // the shipped path, so it must remain a literal no-op against the previous
    // release - hoisting leds() past input() would change which subsystem claims
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
