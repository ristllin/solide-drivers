#include "solide/selftest.h"
#include "solide/leds.h"
#include "solide/display.h"
#include "solide/storage.h"
#include "solide/memory.h"
#include "solide/input.h"
#include <Arduino.h>

namespace solide::selftest {

// Each check prints one RESULT line and returns true for PASS or SKIP (an absent
// optional peripheral is not a failure), false for FAIL.

static bool testLed() {
  bool a = leds::taskAlive();
  Serial.printf("RESULT led %s taskAlive=%d stackFreeB=%u\n",
                a ? "PASS" : "FAIL", a, (unsigned)leds::stackHighWaterBytes());
  return a;
}

static bool testEpd() {
  bool a = display::taskAlive();
  Serial.printf("RESULT epd %s taskAlive=%d\n", a ? "PASS" : "FAIL", a);
  return a;
}

static bool testSd() {
  bool avail = storage::available();
  if (!avail) { Serial.println("RESULT sd SKIP available=0 (no card)"); return true; }
  storage::writeFile("/selftest.txt", "solide-ok");
  bool rt = (storage::readFile("/selftest.txt") == "solide-ok");
  Serial.printf("RESULT sd %s available=1 readback=%d sizeMB=%llu freeMB=%llu\n",
                rt ? "PASS" : "FAIL", rt, storage::cardSizeMB(), storage::freeMB());
  return rt;
}

static bool testMemory() {
  bool nvs = memory::ok();
  bool rt = false;
  if (nvs) { memory::setInt("st_probe", 4242); rt = (memory::getInt("st_probe", 0) == 4242); }
  Serial.printf("RESULT memory %s nvs=%d roundtrip=%d jsonBackend=%d\n",
                (nvs && rt) ? "PASS" : "FAIL", nvs, rt, storage::available());
  return nvs && rt;
}

static bool testInput() {
  bool a = input::taskAlive();
  Serial.printf("RESULT input %s taskAlive=%d pressed=%d\n", a ? "PASS" : "FAIL", a, input::pressed());
  return a;
}

static bool testAudio() {
  // The audio driver + acoustic loopback land in a later milestone.
  Serial.println("RESULT audio SKIP notBuilt=1");
  return true;
}

bool run(const char* name) {
  String n(name); n.trim();
  if (n == "led")                    return testLed();
  if (n == "epd" || n == "display")  return testEpd();
  if (n == "sd"  || n == "storage")  return testSd();
  if (n == "memory")                 return testMemory();
  if (n == "input" || n == "enc")    return testInput();
  if (n == "audio")                  return testAudio();
  if (n == "all") {
    int pass = 0, total = 0;
    pass += testLed();    total++;
    pass += testEpd();    total++;
    pass += testSd();     total++;
    pass += testMemory(); total++;
    pass += testInput();  total++;
    pass += testAudio();  total++;
    Serial.printf("RESULT all %s (%d/%d)\n", pass == total ? "PASS" : "FAIL", pass, total);
    return pass == total;
  }
  Serial.printf("RESULT %s FAIL unknownTest=1\n", name);
  return false;
}

void poll() {
  static String buf;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      String line = buf; buf = ""; line.trim();
      if (line.length() == 0) continue;
      if (line.startsWith("TEST ")) {
        run(line.substring(5).c_str());
      } else if (line == "INFO") {
        Serial.printf("INFO board=%s psramMB=%.1f heap=%u uptime=%lus\n",
                      ESP.getChipModel(), ESP.getPsramSize() / (1024.0 * 1024.0),
                      (unsigned)ESP.getFreeHeap(), millis() / 1000);
      }
    } else {
      buf += c;
    }
  }
}

}  // namespace solide::selftest
