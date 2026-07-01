#pragma once
#include <cstdint>
#include <cstddef>

// Portable, host-testable WAV (RIFF/PCM) header parse + build. No Arduino deps.
// The audio driver uses parseHeader() to play a .wav from any stream, and
// writeHeader() to wrap raw recorded PCM into a playable file.
namespace solide::wav {

struct WavInfo {
  uint32_t sampleRate    = 0;
  uint16_t channels      = 0;
  uint16_t bitsPerSample = 0;
  uint32_t dataOffset    = 0;   // byte offset of PCM data within the stream
  uint32_t dataBytes     = 0;   // size of the data chunk in bytes
};

// Parse a canonical RIFF/WAVE PCM header from the first `n` bytes. Walks chunks
// until "data". Returns false on non-WAV, non-PCM, or truncation. Does not judge
// channels/bit-depth — it reports them; the caller decides what it can play.
bool parseHeader(const uint8_t* buf, size_t n, WavInfo& out);

// Write a 44-byte canonical PCM WAV header for `dataBytes` of audio into out[0..43].
void writeHeader(uint8_t out[44], uint32_t sampleRate, uint32_t dataBytes,
                 uint16_t channels = 1, uint16_t bitsPerSample = 16);

constexpr size_t kHeaderBytes = 44;

}  // namespace solide::wav
