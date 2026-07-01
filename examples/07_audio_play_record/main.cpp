// solide-drivers example: audio TX (speaker) + RX (mic), validated independently.
// Needs the 5 V bus for the amp. Plays three 1 kHz beeps (you should HEAR them),
// then records ~1 s from the PDM mic and reports RMS/peak (non-zero = mic works).
#include <Arduino.h>
#include <math.h>
#include <solide/audio.h>
#include <solide/tone.h>

using namespace solide;

static int16_t toneBuf[3200];    // 0.2 s @ 16 kHz
static int16_t recBuf[16000];    // 1 s @ 16 kHz

static void beeps() {
  const int rate = 16000, freq = 1000, n = 3200;
  for (int i = 0; i < n; i++) toneBuf[i] = (int16_t)(8000.0f * sinf(2.0f * (float)M_PI * freq * i / rate));
  for (int b = 0; b < 3; b++) { audio::playPcm(toneBuf, n, rate); delay(150); }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("solide-drivers: audio example (5 V needed for the amp)");
  audio::begin();
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last > 8000) {
    last = millis();
    Serial.println("-> TX: playing 3x 1 kHz beeps (you should hear them)...");
    beeps();
    Serial.println("-> RX: recording 1 s from the PDM mic...");
    size_t got = audio::recordToBuffer(recBuf, 16000, 1200, nullptr);
    uint16_t r = tone::rms(recBuf, got), p = tone::peak(recBuf, got);
    long sum = 0; int16_t mn = 32767, mx = -32768;
    for (size_t i = 0; i < got; i++) { int16_t v = recBuf[i]; sum += v; if (v < mn) mn = v; if (v > mx) mx = v; }
    long mean = got ? sum / (long)got : 0;
    Serial.printf("RESULT audio-rx samples=%u rms=%u peak=%u min=%d max=%d mean=%ld s0=%d s1=%d s8k=%d\n",
                  (unsigned)got, r, p, mn, mx, mean, recBuf[0],
                  got > 1 ? recBuf[1] : 0, got > 8000 ? recBuf[8000] : 0);
  }
  delay(10);
}
