// solide-drivers example: e-paper "hello world" - a titled text block + a mascot.
// 3.3 V, no 5 V needed.
#include <Arduino.h>
#include <solide/display.h>
#include <solide/status_art.h>

using namespace solide;

void setup() {
  Serial.begin(115200);
  delay(1000);
  display::begin();
  display::requestText("Hello", "solide-drivers e-paper: a bold title plus a wrapped body, "
                                "fast crisp black-and-white (~2.2 s) on the 2.9\" panel.");
}

void loop() {
  static uint32_t t = 0; static int step = 0;
  if (millis() - t > 7000) {
    t = millis();
    if (step % 2 == 0) display::showArt(art::IDLE, true);          // mascot, fast B/W
    else               display::requestText("Hello", "Non-ASCII bytes are dropped so any text is safe to render.");
    step++;
  }
  delay(10);
}
