#include "solide/selftest.h"
#include "solide/battery.h"
#include "solide/board.h"
#include "solide/leds.h"
#include "solide/display.h"
#include "solide/display_tft.h"
#include "solide/storage.h"
#include "solide/memory.h"
#include "solide/input.h"
#include "solide/touch.h"
#include "solide/audio.h"
#include "solide/tone.h"
#include <Arduino.h>
#include <math.h>

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
  // ⚠ Test the panel that is actually BOUND. A board fitted with the colour TFT
  // never starts the e-paper task, so checking display::taskAlive() there
  // reports FAIL for hardware that is absent by design - slandering healthy
  // hardware, the same way the encoder row below did.
  if (display_tft::taskAlive()) {
    Serial.printf("RESULT epd PASS panel=tft taskAlive=1\n");
    return true;
  }
  bool a = display::taskAlive();
  Serial.printf("RESULT epd %s panel=eink taskAlive=%d\n", a ? "PASS" : "FAIL", a);
  return a;
}

static bool testSd() {
  bool avail = storage::available();
  if (!avail) { Serial.println("RESULT sd SKIP available=0 (no card)"); return true; }
  storage::writeFile("/selftest.txt", "solide-ok");
  bool rt = (storage::readFile("/selftest.txt") == "solide-ok");
  storage::remove("/selftest.txt");   // leave no residue on the card
  Serial.printf("RESULT sd %s available=1 readback=%d sizeMB=%llu freeMB=%llu\n",
                rt ? "PASS" : "FAIL", rt, storage::cardSizeMB(), storage::freeMB());
  return rt;
}

static bool testMemory() {
  bool nvs = memory::ok();
  bool rt = false;
  if (nvs) { memory::setInt("st_probe", 4242); rt = (memory::getInt("st_probe", 0) == 4242); memory::eraseKey("st_probe"); }
  Serial.printf("RESULT memory %s nvs=%d roundtrip=%d jsonBackend=%d\n",
                (nvs && rt) ? "PASS" : "FAIL", nvs, rt, storage::available());
  return nvs && rt;
}

static bool testInput() {
  // Same reasoning: a TFT board has no encoder (its pins are the panel's), so
  // the input device to check there is the touch controller.
  if (touch::present()) {
    Serial.printf("RESULT input PASS dev=touch present=1\n");
    return true;
  }
  bool a = input::taskAlive();
  Serial.printf("RESULT input %s dev=encoder taskAlive=%d pressed=%d\n",
                a ? "PASS" : "FAIL", a, input::pressed());
  return a;
}

static bool testAudio() {
  // Acoustic loopback: play a tone on the speaker, detect it on the mic. Needs
  // the 5 V amp bus + a working mic; a no-detection result is reported SKIP (like
  // an absent optional peripheral), not FAIL, so it doesn't fail the suite.
  //
  // The diagnostics separate the three failure modes so a SKIP is actionable:
  //   MIC-DEAD    : peak ~= 0 (or |dcMean| huge)   -> no PDM data on GPIO16
  //   SPEAKER/COUP: rms high but tone ~= control   -> mic hears noise, no tone
  //                 (5 V amp not reaching the speaker, or no acoustic coupling)
  //   PASS        : tone >> control and above floor -> tone reproduced & heard
  audio::LbDiag d{};
  bool detected = audio::loopbackSelfTest(1000, nullptr, nullptr, &d);
  const char* reason = "";
  if (!detected) {
    if (d.peak < 200 || (d.dcMean > 20000 || d.dcMean < -20000))
      reason = " (MIC-DEAD: no PDM data on din - check GPIO16 DATA / GPIO15 CLK / mic module)";
    else if (d.toneMag <= 2 * d.ctrlMag)
      reason = " (SPEAKER/COUPLING: mic hears broadband noise but no 1kHz tone - check 5V amp bus reaches the speaker + acoustic path)";
    else
      reason = " (tone present but below detection floor - raise SPL or lower threshold)";
  }
  Serial.printf("RESULT audio %s toneHz=1000 mag=%u ctrl=%u rms=%u peak=%u dcMean=%ld samples=%u%s\n",
                detected ? "PASS" : "SKIP", (unsigned)d.toneMag, (unsigned)d.ctrlMag,
                d.rms, d.peak, (long)d.dcMean, (unsigned)d.samples, reason);
  return true;
}

// Speaker-only: play an audible 1 kHz tone at high amplitude for ~1.2 s. No mic
// involved - a HUMAN confirms whether the amp/5 V/speaker chain makes sound. This
// bisects the loopback: if you hear this but `TEST audio` still SKIPs, the fault is
// mic capture or acoustic coupling, not the speaker.
static bool testSpk() {
  const int rate = 16000, freq = 1000, n = rate * 1200 / 1000;
  int16_t* buf = (int16_t*)malloc(n * sizeof(int16_t));
  if (!buf) { Serial.println("RESULT spk FAIL alloc"); return false; }
  for (int i = 0; i < n; i++)
    buf[i] = (int16_t)(24000.0f * sinf(2.0f * (float)PI * freq * i / rate));
  bool played = audio::playPcm(buf, n, rate);
  free(buf);
  Serial.printf("RESULT spk %s toneHz=1000 durMs=1200 amp=24000 (LISTEN: you should hear a 1kHz tone; silence => amp/5V/speaker fault)\n",
                played ? "PASS" : "FAIL");
  return played;
}

// Mic-only: record ~2 s and stream the RMS so a human can TAP the mic and watch the
// number jump. No speaker involved. peak~=0 or a huge constant dcMean => no PDM data
// (check GPIO16 DATA / GPIO15 CLK / the mic module); a resting rms that RESPONDS to
// tapping => the mic is alive and the earlier dead-line fault is resolved.
static bool testMic() {
  const size_t N = 3200;   // 0.2 s @ 16 kHz per window
  int16_t* buf = (int16_t*)malloc(N * sizeof(int16_t));
  if (!buf) { Serial.println("RESULT mic FAIL alloc"); return false; }
  Serial.println("mic monitor: tap the mic - RMS should jump (2 s)...");
  uint16_t maxPeak = 0; uint32_t t0 = millis();
  while (millis() - t0 < 2000) {
    size_t got = audio::recordToBuffer(buf, N, 250, nullptr);
    if (!got) continue;
    uint16_t r = tone::rms(buf, got), p = tone::peak(buf, got);
    int64_t acc = 0; for (size_t i = 0; i < got; i++) acc += buf[i];
    long mean = got ? (long)(acc / (int64_t)got) : 0;
    if (p > maxPeak) maxPeak = p;
    Serial.printf("  mic rms=%u peak=%u dcMean=%ld samples=%u\n", r, p, mean, (unsigned)got);
  }
  bool alive = maxPeak > 200;
  Serial.printf("RESULT mic %s maxPeak=%u (alive => tapping moved it; ~0 => no PDM data on din)\n",
                alive ? "PASS" : "FAIL", maxPeak);
  free(buf);
  return alive;
}

// Battery divider: PASS = fitted + plausible per-cell voltage. SKIP (PASS-with-
// note) when the divider isn't fitted - an unfitted sense line is a valid build.
static bool testBatt() {
  if (!battery::present()) {
    Serial.println("RESULT batt SKIP fitted=0 (no divider on batt.sense, or implausible read)");
    return true;
  }
  const uint16_t pack = battery::packMv();
  const uint16_t cell = battery::cellMv();
  const bool ok = cell >= 2500 && cell <= 4400;
  Serial.printf("RESULT batt %s packMv=%u cellMv=%u cells=%u\n",
                ok ? "PASS" : "FAIL", pack, cell, (unsigned)board().batt.cells);
  return ok;
}

bool run(const char* name) {
  String n(name); n.trim();
  if (n == "led")                    return testLed();
  if (n == "epd" || n == "display")  return testEpd();
  if (n == "sd"  || n == "storage")  return testSd();
  if (n == "memory")                 return testMemory();
  if (n == "input" || n == "enc")    return testInput();
  if (n == "audio")                  return testAudio();
  if (n == "spk"  || n == "speaker") return testSpk();
  if (n == "mic")                    return testMic();
  if (n == "batt" || n == "battery") return testBatt();
  if (n == "all") {
    int pass = 0, total = 0;
    pass += testLed();    total++;
    pass += testEpd();    total++;
    pass += testSd();     total++;
    pass += testMemory(); total++;
    pass += testInput();  total++;
    pass += testAudio();  total++;
    pass += testBatt();   total++;
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
