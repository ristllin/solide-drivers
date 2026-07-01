// solide-drivers example: the WS2812B ring — smooth patterns + colour schemes +
// agent-status segmentation. Needs the 5 V bus to light. Also a good check that
// the modernized NeoPixel RMT path (IDF5) has no per-show heap leak: watch that
// heap + the render-task stack high-water stay flat across the alive lines.
#include <Arduino.h>
#include <solide/leds.h>

using namespace solide;

static const uint32_t A = 0xA1, B = 0xB2, C = 0xC3;

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("solide-drivers: LED ring example (needs 5 V)");
  bool ok = leds::begin();
  leds::setBrightness(40);
  leds::show(leds::Pattern::Rainbow);
  Serial.printf("leds::begin=%d taskAlive=%d\n", ok, leds::taskAlive());
}

void loop() {
  // Alternate: ambient cycle (12 s, advancing scheme) <-> a 3-agent status scene.
  static uint32_t phaseAt = 0, sceneAt = 0; static bool agents = false; static int scene = 0;
  uint32_t now = millis();
  if (!agents) {
    if (now - phaseAt > 12000) {
      agents = true; phaseAt = sceneAt = now;
      leds::agentStatus(A, ring::Status::Running); leds::agentAccent(A, 128);
      leds::agentStatus(B, ring::Status::AwaitingApproval);
      leds::agentStatus(C, ring::Status::WaitingInput);
      Serial.println("LED: agent scene — Running(comet) / AwaitingApproval(blink) / WaitingInput(breathe)");
    }
  } else if (now - phaseAt > 12000) {
    agents = false; phaseAt = now;
    leds::agentClear();
    leds::setScheme((ring::Scheme)(((int)leds::scheme() + 1) % (int)ring::Scheme::COUNT));
    leds::show(leds::Pattern::Rainbow);
    Serial.printf("LED: ambient cycle — scheme=%s\n", ring::schemeName(leds::scheme()));
  } else if (now - sceneAt > 4000) {
    sceneAt = now;                                   // shuffle the scene a little
    leds::agentStatus(B, scene & 1 ? ring::Status::Done : ring::Status::AwaitingApproval);
    scene++;
  }

  static uint32_t last = 0;
  if (millis() - last > 2500) {
    last = millis();
    Serial.printf("[alive %lus] leds taskAlive=%d stackFreeB=%u heap=%u segs=%d bri=%d sch=%s\n",
                  millis() / 1000, leds::taskAlive(), (unsigned)leds::stackHighWaterBytes(),
                  (unsigned)ESP.getFreeHeap(), leds::agentCount(),
                  leds::maxBrightness(), ring::schemeName(leds::scheme()));
  }
  delay(5);
}
