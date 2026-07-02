# solide::audio — I2S speaker + PDM mic

Rewritten on the ESP-IDF 5 channel API: `driver/i2s_std` (speaker TX, I2S1) +
`driver/i2s_pdm` (mic RX, I2S0). Independent channels, so play + record run
concurrently. Synchronous (no background worker).

## API
```cpp
bool begin();  void end();
// speaker (mono 16-bit -> duplicated to L+R):
bool playPcm(const int16_t* mono, size_t sampleCount, uint32_t sampleRate);
bool playWavFile(fs::FS& fs, const char* path);          // canonical 16-bit mono PCM WAV
bool spkOpen(uint32_t sampleRate);                        // streaming (rate clamped [8k,48k])
void spkFeedBytes(const uint8_t* pcmLe16, size_t n);
void spkClose();
// mic (16 kHz / 16-bit / mono):
size_t recordToBuffer(int16_t* out, size_t maxSamples, uint32_t maxMs, const volatile bool* stopFlag);
size_t recordToFile(fs::FS& fs, const char* path, uint32_t maxMs, const volatile bool* stopFlag);
// acoustic self-test:
bool loopbackSelfTest(uint16_t toneHz = 1000, uint32_t* magOut = nullptr, uint16_t* rmsOut = nullptr);
```

## Example
`examples/07_audio_play_record` (beeps + record + level).

## Limitations & status
- **3.3 V ONLY** on the audio board — the PDM mic DATA follows VCC; 5 V damages the S3.
  The amp needs the **5 V bus** for audible volume.
- Mic is fixed 16 kHz / 16-bit / mono. Speaker rate is per call; mono only (duplicated).
- **TX validated** on hardware (Arduino 3.3.9 / IDF 5.5.4). **Mic capture is a hardware
  check pending** — a constant `-30935` reading means "no data on the mic DATA line"
  (ESP-IDF #12382); verify GPIO16 DATA / GPIO15 CLK wiring + the mic module.
- `loopbackSelfTest` is code-complete + crash-safe but its acoustic threshold is
  unvalidated until the mic works + 5 V is on — tune `mag > 1000` then.
