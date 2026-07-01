// solide-drivers integration example: brings up every peripheral and exposes the
// serial self-test protocol. Type over serial:
//   TEST all | TEST led | TEST epd | TEST sd | TEST memory | TEST input | INFO
// Turn/press the encoder to see live input events. This is the example the device
// test harness (test_device/) drives. 3.3 V is enough (LED lights only with 5 V).
#include <Arduino.h>
#include <solide/solide.h>

using namespace solide;

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("solide-drivers: self-test console — send 'TEST all' or 'INFO'");
  BeginResult b = begin();
  Serial.printf("begin: display=%d leds=%d storage=%d memory=%d input=%d\n",
                b.display, b.leds, b.storage, b.memory, b.input);
  leds::show(leds::Pattern::Spinner, 0, 180, 255);
  display::requestText("solide-drivers", "Self-test console ready. Send 'TEST all' over serial.");
  selftest::run("all");
}

void loop() {
  selftest::poll();                       // TEST/INFO protocol

  input::Event e;                         // show encoder events live
  while (input::pop(e)) {
    const char* n = e == input::Event::RotateCW    ? "CW"
                  : e == input::Event::RotateCCW   ? "CCW"
                  : e == input::Event::Click       ? "CLICK"
                  : e == input::Event::LongPress   ? "LONG" : "?";
    Serial.printf("ENC %s\n", n);
  }

  static uint32_t last = 0;
  if (millis() - last > 3000) {
    last = millis();
    Serial.printf("[alive %lus] heap=%u leds=%d epd=%d\n",
                  millis() / 1000, (unsigned)ESP.getFreeHeap(),
                  leds::taskAlive(), display::taskAlive());
  }
  delay(5);
}
