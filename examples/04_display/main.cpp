// solide-drivers example: the e-paper display — fast B/W text/menu + 3-colour art.
// 3.3 V, no 5 V needed. Watch the serial for GxEPD2 refresh timings
// (fast B/W ~2.2 s, 3-colour ~18.5 s).
#include <Arduino.h>
#include <solide/display.h>
#include <solide/status_art.h>
#include <solide/menu.h>

using namespace solide;

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("solide-drivers: display example (3.3 V)");
  bool ok = display::begin();
  Serial.printf("display::begin=%d taskAlive=%d\n", ok, display::taskAlive());
  display::requestText("solide-drivers", "E-paper up. Fast crisp B/W via the WS_20_30 LUT. This body wraps across the 296x128 panel.");
}

void loop() {
  static int step = 0;
  static uint32_t last = 0;
  if (millis() - last > 6000) {
    last = millis();
    switch (step % 4) {
      case 0:
        display::requestText("Text mode", "Titled text block, fast B/W. Non-ASCII bytes are dropped so LLM output can't crash the font.");
        Serial.println("-> requestText (fast B/W ~2.2 s)");
        break;
      case 1:
        display::showArt(art::WORKING, true);
        Serial.println("-> showArt WORKING (fast B/W)");
        break;
      case 2: {
        menu::MenuView v;
        v.visible = true; v.title = "Menu";
        v.items = {"Record", "Set Wi-Fi", "Versions", "Restart"};
        v.selected = 1;
        display::requestMenu(v, true);
        Serial.println("-> requestMenu (full B/W)");
        break;
      }
      case 3:
        display::showArt(art::IDLE, false);
        Serial.println("-> showArt IDLE (3-colour ~18.5 s)");
        break;
    }
    step++;
  }
  static uint32_t alive = 0;
  if (millis() - alive > 2500) {
    alive = millis();
    Serial.printf("[alive %lus] display taskAlive=%d heap=%u\n",
                  millis() / 1000, display::taskAlive(), (unsigned)ESP.getFreeHeap());
  }
  delay(5);
}
