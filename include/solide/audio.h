#pragma once
#include <Arduino.h>
#include <FS.h>

// ============================================================================
// solide::audio — I2S speaker (TX) + I2S mic (RX). Separate boards on Nimbus V0.1:
// MAX98357A amp (5 V bus for volume) + INMP441/ICS-43434 I2S MEMS mic (3.3 V ONLY:
// the mic VDD/data lines follow VCC, 5 V would damage the S3). Rewritten on the
// ESP-IDF 5 channel API (driver/i2s_std for both TX and RX); the PUBLIC API is
// unchanged from the original legacy-I2S driver. Mic is fixed 16 kHz / 16-bit /
// mono (top 16 bits of the mic's 24-bit slot); speaker rate is per call.
// Synchronous (blocking); no background worker. TX (I2S1) and RX (I2S0) are
// independent channels, so play + record run concurrently (loopback self-test).
// SPEAKER CALLS ARE TASK-SAFE at whole-clip granularity: a recursive mutex is
// held from spkOpen to the matching spkClose, so concurrent playPcm/playWavFile/
// spkOpen callers (SFX task vs orchestrator turn task vs web beep) queue instead
// of tearing down the TX channel under each other. A clip therefore BLOCKS until
// the previous clip finishes. The streaming trio must open/feed/close from ONE task.
// ============================================================================
namespace solide::audio {

bool begin();   // optional pre-flight; channels open lazily on first use
void end();      // release both channels

// ---- speaker (I2S TX; mono 16-bit duplicated to L+R for the stereo amp) ----
bool playPcm(const int16_t* mono, size_t sampleCount, uint32_t sampleRate);
bool playWavFile(fs::FS& fs, const char* path);        // canonical 16-bit mono PCM WAV

// Master playback attenuation (0.0..1.0), applied to EVERY speaker output (SFX,
// TTS, beep — they all funnel through the mono->stereo write). The MAX98357A's
// fixed ~9 dB gain overdrives a small speaker at full scale, so full-scale WAVs
// (e.g. game voice rips) clip/distort; keep this below 1.0. Persists until
// changed; the acoustic loopback self-test forces full scale internally.
void  setVolume(float v);
float getVolume();
bool spkOpen(uint32_t sampleRate);                      // streaming: open at rate (clamped [8k,48k])
void spkFeedBytes(const uint8_t* pcmLe16, size_t n);    // feed LE 16-bit mono chunks
void spkClose();                                        // flush + close

// ---- mic (I2S std RX; 16 kHz / 16-bit mono) ----
size_t recordToBuffer(int16_t* out, size_t maxSamples, uint32_t maxMs, const volatile bool* stopFlag);
size_t recordToFile(fs::FS& fs, const char* path, uint32_t maxMs, const volatile bool* stopFlag);

// ---- speaker->mic acoustic loopback self-test ----
// Plays a `toneHz` tone on the speaker while recording on the mic (TX on I2S1,
// RX on I2S0 run concurrently), then Goertzel-detects the tone in the capture.
// Returns true if the tone is present. `magOut`/`rmsOut` (optional) report the
// measured tone magnitude and record RMS. NEEDS the 5 V amp bus + a working mic.
//
// Diagnostics that separate the failure modes on hardware (all optional via
// `diagOut`):
//   toneMag  Goertzel @ toneHz         — the loopback tone energy
//   ctrlMag  Goertzel @ toneHz+2100 Hz — control band the speaker never plays;
//            a REAL tone gives toneMag >> ctrlMag, broadband hash/ambient gives
//            toneMag ~= ctrlMag (so ratio ~1 == "mic hears noise, not the tone")
//   rms      overall level (mic alive vs silent)
//   peak     peak abs sample (0..32767; ~0 == mic delivering no data)
//   dcMean   mean sample (large |mean| == HP off / stuck DATA line)
//   samples  samples actually captured
// Interpretation: toneMag>1000 AND toneMag>2*ctrlMag => tone reproduced (PASS).
//   rms high but toneMag~=ctrlMag => mic works, speaker NOT radiating the tone
//   (amp/5 V/acoustic coupling). rms~=0 / peak~=0 => mic delivering no data.
struct LbDiag {
  uint32_t toneMag;
  uint32_t ctrlMag;
  uint16_t rms;
  uint16_t peak;
  int32_t  dcMean;
  uint32_t samples;
};
bool loopbackSelfTest(uint16_t toneHz = 1000, uint32_t* magOut = nullptr,
                      uint16_t* rmsOut = nullptr, LbDiag* diagOut = nullptr);

constexpr uint32_t kMicSampleRate    = 16000;
constexpr uint8_t  kMicBitsPerSample = 16;

}  // namespace solide::audio
