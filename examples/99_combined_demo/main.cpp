// solide-drivers example: everything together. Brings up all peripherals, shows a
// status screen + rainbow ring, and uses the encoder to drive the LEDs live —
// turn = brightness, click = next colour scheme, long-press = sleep art.
// LED ring lights only with the 5 V bus; everything else runs on USB.
#include <Arduino.h>
#include <solide/solide.h>

using namespace solide;

void setup() {
  Serial.begin(115200);
  delay(1200);
  BeginResult b = begin();
  Serial.printf("begin: display=%d leds=%d storage=%d memory=%d input=%d\n",
                b.display, b.leds, b.storage, b.memory, b.input);
  leds::show(leds::Pattern::Rainbow);
  display::requestText("solide-drivers",
                       "Combined demo. Turn the knob = LED brightness; click = colour scheme; hold = sleep.");
}

void loop() {
  input::Event e;
  while (input::pop(e)) {
    if (e == input::Event::RotateCW)  { int v = leds::maxBrightness() + 8; leds::setBrightness(v > 120 ? 120 : v); }
    else if (e == input::Event::RotateCCW) { int v = (int)leds::maxBrightness() - 8; leds::setBrightness(v < 4 ? 4 : v); }
    else if (e == input::Event::Click) { leds::setScheme((ring::Scheme)(((int)leds::scheme() + 1) % (int)ring::Scheme::COUNT)); }
    else if (e == input::Event::LongPress) { display::showArt(art::SLEEP, false); }
  }
  static uint32_t t = 0;
  if (millis() - t > 3000) {
    t = millis();
    Serial.printf("[alive %lus] bri=%d sch=%s heap=%u\n", millis() / 1000,
                  leds::maxBrightness(), ring::schemeName(leds::scheme()), (unsigned)ESP.getFreeHeap());
  }
  delay(5);
}
