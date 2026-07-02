#pragma once
#include <Arduino.h>
#include <FS.h>

// ============================================================================
// solide::audio — I2S speaker (TX) + PDM mic (RX) on the combined audio board.
// 3.3 V ONLY: the PDM mic DATA line follows VCC, and 5 V would damage the S3.
// The amp needs the 5 V bus for volume. Rewritten on the ESP-IDF 5 channel API
// (driver/i2s_std for TX + driver/i2s_pdm for RX); the PUBLIC API is unchanged
// from the original legacy-I2S driver. Mic is fixed 16 kHz / 16-bit / mono; the
// speaker rate is per call. Synchronous (blocking); no background worker.
// TX (I2S1) and PDM-RX (I2S0) are independent channels, so play + record can run
// concurrently (used by the speaker->mic acoustic loopback self-test).
// ============================================================================
namespace solide::audio {

bool begin();   // optional pre-flight; channels open lazily on first use
void end();      // release both channels

// ---- speaker (I2S TX; mono 16-bit duplicated to L+R for the stereo amp) ----
bool playPcm(const int16_t* mono, size_t sampleCount, uint32_t sampleRate);
bool playWavFile(fs::FS& fs, const char* path);        // canonical 16-bit mono PCM WAV
bool spkOpen(uint32_t sampleRate);                      // streaming: open at rate (clamped [8k,48k])
void spkFeedBytes(const uint8_t* pcmLe16, size_t n);    // feed LE 16-bit mono chunks
void spkClose();                                        // flush + close

// ---- mic (PDM RX; 16 kHz / 16-bit mono) ----
size_t recordToBuffer(int16_t* out, size_t maxSamples, uint32_t maxMs, const volatile bool* stopFlag);
size_t recordToFile(fs::FS& fs, const char* path, uint32_t maxMs, const volatile bool* stopFlag);

// ---- speaker->mic acoustic loopback self-test ----
// Plays a `toneHz` tone on the speaker while recording on the mic (TX on I2S1,
// PDM-RX on I2S0 run concurrently), then Goertzel-detects the tone in the capture.
// Returns true if the tone is present. `magOut`/`rmsOut` (optional) report the
// measured tone magnitude and record RMS. NEEDS the 5 V amp bus + a working mic.
bool loopbackSelfTest(uint16_t toneHz = 1000, uint32_t* magOut = nullptr, uint16_t* rmsOut = nullptr);

constexpr uint32_t kMicSampleRate    = 16000;
constexpr uint8_t  kMicBitsPerSample = 16;

}  // namespace solide::audio
