// solide-drivers: quiet firmware - brings the LED ring up and immediately turns it
// OFF, then idles. Flashed to leave the board dark (so the ring never lights, even
// when the 5 V bus is restored). 3.3 V.
#include <Arduino.h>
#include <solide/leds.h>

using namespace solide;

void setup() {
  Serial.begin(115200);
  delay(800);
  leds::begin();
  leds::off();          // render task clears all pixels
  Serial.println("solide-drivers: LED ring OFF, idle.");
}

void loop() {
  static uint32_t t = 0;
  if (millis() - t > 5000) {
    t = millis();
    Serial.printf("[idle %lus] leds off, heap=%u\n", millis() / 1000, (unsigned)ESP.getFreeHeap());
  }
  delay(50);
}
