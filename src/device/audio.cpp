#include "solide/audio.h"
#include "solide/boards/board_solide_s3.h"
#include "solide/wav.h"
#include "solide/tone.h"
#include "driver/i2s_std.h"
#include "driver/i2s_pdm.h"
#include "esp_heap_caps.h"
#include <math.h>

// IDF5 channel-API rewrite of the legacy-I2S driver. Every config struct is
// initialized FIELD-BY-FIELD (not via the IDF I2S_*_DEFAULT_CONFIG macros): those
// macros use C designated initializers whose field order differs from the struct
// order, which is an error under C++ — so we set members explicitly. All numeric
// config, the mono->stereo duplication, the odd-byte carry, the 60 ms mic settle,
// and the [8k,48k] clamp are preserved exactly from the proven original.

namespace solide::audio {

static constexpr int SPK_BCLK  = kBoardSolideS3.spk.bclk;
static constexpr int SPK_LRCLK = kBoardSolideS3.spk.lrclk;
static constexpr int SPK_DIN   = kBoardSolideS3.spk.din;
static constexpr int MIC_CLK   = kBoardSolideS3.mic.clk;
static constexpr int MIC_DATA  = kBoardSolideS3.mic.data;

static constexpr i2s_port_t TX_PORT = I2S_NUM_1;   // speaker (std)
static constexpr i2s_port_t RX_PORT = I2S_NUM_0;   // mic (pdm)

static i2s_chan_handle_t g_tx = nullptr;
static i2s_chan_handle_t g_rx = nullptr;
static bool     s_spkOpen   = false;
static uint8_t  s_carry     = 0;
static bool     s_haveCarry = false;

// ---- speaker (TX / i2s_std) --------------------------------------------------

static bool spkInit(uint32_t rate) {
  if (g_tx) return true;
  i2s_chan_config_t cc = {};
  cc.id = TX_PORT;
  cc.role = I2S_ROLE_MASTER;
  cc.dma_desc_num = 8;      // was dma_buf_count
  cc.dma_frame_num = 256;   // was dma_buf_len
  cc.auto_clear = true;     // clear the DMA on underrun (was tx_desc_auto_clear)
  if (i2s_new_channel(&cc, &g_tx, nullptr) != ESP_OK) { g_tx = nullptr; return false; }

  i2s_std_config_t sc = {};
  sc.clk_cfg.sample_rate_hz = rate;
  sc.clk_cfg.clk_src        = I2S_CLK_SRC_DEFAULT;
  sc.clk_cfg.mclk_multiple  = I2S_MCLK_MULTIPLE_256;
  sc.slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_16BIT;
  sc.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO;
  sc.slot_cfg.slot_mode      = I2S_SLOT_MODE_STEREO;   // mono duplicated into L+R
  sc.slot_cfg.slot_mask      = I2S_STD_SLOT_BOTH;
  sc.slot_cfg.ws_width       = 16;
  sc.slot_cfg.ws_pol         = false;
  sc.slot_cfg.bit_shift      = true;                   // Philips
  sc.slot_cfg.left_align     = true;
  sc.slot_cfg.big_endian     = false;
  sc.slot_cfg.bit_order_lsb  = false;
  sc.gpio_cfg.mclk = I2S_GPIO_UNUSED;
  sc.gpio_cfg.bclk = (gpio_num_t)SPK_BCLK;
  sc.gpio_cfg.ws   = (gpio_num_t)SPK_LRCLK;
  sc.gpio_cfg.dout = (gpio_num_t)SPK_DIN;
  sc.gpio_cfg.din  = I2S_GPIO_UNUSED;
  if (i2s_channel_init_std_mode(g_tx, &sc) != ESP_OK) { i2s_del_channel(g_tx); g_tx = nullptr; return false; }
  if (i2s_channel_enable(g_tx) != ESP_OK)             { i2s_del_channel(g_tx); g_tx = nullptr; return false; }
  return true;
}

static void spkDeinit() {
  if (!g_tx) return;
  i2s_channel_disable(g_tx);
  i2s_del_channel(g_tx);
  g_tx = nullptr;
}

// mono -> L+R duplication, written in 256-frame blocks (blocking).
static void spkWrite(const int16_t* mono, size_t n) {
  if (!g_tx) return;
  static int16_t st[512];   // 256 stereo frames
  size_t i = 0;
  while (i < n) {
    size_t m = 0;
    while (i < n && m < 256) { st[m * 2] = mono[i]; st[m * 2 + 1] = mono[i]; m++; i++; }
    const uint8_t* p = (const uint8_t*)st;
    size_t left = m * 4;
    while (left) {
      size_t wrote = 0;
      if (i2s_channel_write(g_tx, p, left, &wrote, 1000) != ESP_OK) break;
      p += wrote; left -= wrote;
    }
  }
}

bool spkOpen(uint32_t sampleRate) {
  if (s_spkOpen) spkClose();
  if (sampleRate < 8000 || sampleRate > 48000) sampleRate = 24000;
  s_carry = 0; s_haveCarry = false;
  if (!spkInit(sampleRate)) return false;
  s_spkOpen = true;
  return true;
}

void spkFeedBytes(const uint8_t* b, size_t n) {
  if (!s_spkOpen) return;
  static int16_t s[512];
  size_t si = 0, i = 0;
  if (s_haveCarry && n > 0) {
    s[si++] = (int16_t)(s_carry | (b[0] << 8));
    s_haveCarry = false;
    i = 1;
  }
  for (; i + 1 < n; i += 2) {
    s[si++] = (int16_t)(b[i] | (b[i + 1] << 8));
    if (si == 512) { spkWrite(s, si); si = 0; }
  }
  if (i < n) { s_carry = b[i]; s_haveCarry = true; }
  if (si) spkWrite(s, si);
}

void spkClose() {
  if (!s_spkOpen) return;
  if (s_haveCarry) { uint8_t z = 0; s_haveCarry = false; int16_t last = (int16_t)(s_carry | (z << 8)); spkWrite(&last, 1); }
  delay(60);       // let the DMA drain
  spkDeinit();
  s_spkOpen = false;
}

bool playPcm(const int16_t* mono, size_t sampleCount, uint32_t sampleRate) {
  if (!spkOpen(sampleRate)) return false;
  spkWrite(mono, sampleCount);
  spkClose();
  return true;
}

bool playWavFile(fs::FS& fs, const char* path) {
  File f = fs.open(path, FILE_READ);
  if (!f) return false;
  uint8_t hdr[512];
  size_t hn = f.read(hdr, sizeof(hdr));
  solide::wav::WavInfo info;
  if (!solide::wav::parseHeader(hdr, hn, info) || info.channels != 1 || info.bitsPerSample != 16) {
    f.close(); return false;
  }
  if (!spkOpen(info.sampleRate)) { f.close(); return false; }
  f.seek(info.dataOffset);
  uint8_t buf[1024];
  uint32_t remaining = info.dataBytes;
  while (remaining) {
    size_t want = remaining < sizeof(buf) ? remaining : sizeof(buf);
    size_t got = f.read(buf, want);
    if (got == 0) break;
    spkFeedBytes(buf, got);
    remaining -= got;
  }
  spkClose();
  f.close();
  return true;
}

// ---- mic (RX / i2s_pdm) ------------------------------------------------------

static bool micInit() {
  if (g_rx) return true;
  i2s_chan_config_t cc = {};
  cc.id = RX_PORT;
  cc.role = I2S_ROLE_MASTER;
  cc.dma_desc_num = 8;
  cc.dma_frame_num = 256;
  if (i2s_new_channel(&cc, nullptr, &g_rx) != ESP_OK) { g_rx = nullptr; return false; }

  i2s_pdm_rx_config_t pc = {};
  pc.clk_cfg.sample_rate_hz  = kMicSampleRate;
  pc.clk_cfg.clk_src         = I2S_CLK_SRC_DEFAULT;
  pc.clk_cfg.mclk_multiple   = I2S_MCLK_MULTIPLE_256;
  pc.clk_cfg.dn_sample_mode  = I2S_PDM_DSR_8S;
  pc.clk_cfg.bclk_div        = 8;
  pc.slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_16BIT;
  pc.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO;
  pc.slot_cfg.slot_mode      = I2S_SLOT_MODE_MONO;
  pc.slot_cfg.slot_mask      = I2S_PDM_SLOT_LEFT;      // matches the classic ONLY_LEFT
#if SOC_I2S_SUPPORTS_PDM_RX_HP_FILTER
  pc.slot_cfg.hp_en          = true;                  // high-pass removes the PDM DC offset
  pc.slot_cfg.hp_cut_off_freq_hz = 35.5f;
  pc.slot_cfg.amplify_num    = 1;
#endif
  pc.gpio_cfg.clk = (gpio_num_t)MIC_CLK;
  pc.gpio_cfg.din = (gpio_num_t)MIC_DATA;
  if (i2s_channel_init_pdm_rx_mode(g_rx, &pc) != ESP_OK) { i2s_del_channel(g_rx); g_rx = nullptr; return false; }
  if (i2s_channel_enable(g_rx) != ESP_OK)                { i2s_del_channel(g_rx); g_rx = nullptr; return false; }
  return true;
}

static void micDeinit() {
  if (!g_rx) return;
  i2s_channel_disable(g_rx);
  i2s_del_channel(g_rx);
  g_rx = nullptr;
}

// Discard the PDM start-up transient (~60 ms), same as the classic build.
static void micSettle() {
  int16_t rb[256]; size_t got = 0; uint32_t t0 = millis();
  while (millis() - t0 < 60) i2s_channel_read(g_rx, rb, sizeof(rb), &got, 20);
}

size_t recordToBuffer(int16_t* out, size_t maxSamples, uint32_t maxMs, const volatile bool* stopFlag) {
  if (!micInit()) return 0;
  micSettle();
  size_t total = 0; uint32_t t0 = millis();
  while (total < maxSamples && millis() - t0 < maxMs && !(stopFlag && *stopFlag)) {
    size_t want = (maxSamples - total) * sizeof(int16_t);
    size_t chunk = want < 512 ? want : 512;
    size_t got = 0;
    if (i2s_channel_read(g_rx, out + total, chunk, &got, 100) == ESP_OK && got > 0)
      total += got / sizeof(int16_t);
  }
  micDeinit();
  return total;
}

size_t recordToFile(fs::FS& fs, const char* path, uint32_t maxMs, const volatile bool* stopFlag) {
  if (!micInit()) return 0;
  File f = fs.open(path, FILE_WRITE);
  if (!f) { micDeinit(); return 0; }
  micSettle();
  const size_t CAP = 4096;
  int16_t* rb = (int16_t*)heap_caps_malloc(CAP * sizeof(int16_t), MALLOC_CAP_SPIRAM);
  static int16_t fb[256];
  int16_t* buf = rb ? rb : fb;
  size_t bufBytes = (rb ? CAP : 256) * sizeof(int16_t);
  size_t bytesWritten = 0; uint32_t t0 = millis();
  while (millis() - t0 < maxMs && !(stopFlag && *stopFlag)) {
    size_t got = 0;
    if (i2s_channel_read(g_rx, buf, bufBytes, &got, 100) == ESP_OK && got > 0) {
      f.write((const uint8_t*)buf, got);
      bytesWritten += got;
    }
  }
  if (rb) heap_caps_free(rb);
  f.close();
  micDeinit();
  return bytesWritten;
}

// ---- acoustic loopback self-test --------------------------------------------
// TX (I2S1) + PDM-RX (I2S0) are independent, so a play task can run while the
// caller records. NOTE: unvalidated as of the initial build — needs the 5 V amp
// bus + a working mic; the detection threshold is a starting point to tune on
// real hardware.
// The play task only WRITES to an already-open TX channel — it never allocates a
// channel, so it can't race the RX allocation. Both channels are opened (and
// closed) sequentially by the caller below.
struct LbCtx { const int16_t* tone; size_t n; volatile bool done; };

static void lbPlayTask(void* p) {
  LbCtx* c = (LbCtx*)p;
  spkWrite(c->tone, c->n);
  c->done = true;
  vTaskDelete(nullptr);
}

bool loopbackSelfTest(uint16_t toneHz, uint32_t* magOut, uint16_t* rmsOut) {
  static volatile bool s_busy = false;
  if (s_busy) return false;                  // not re-entrant
  s_busy = true;

  const uint32_t rate = 16000;
  const size_t   n    = rate * 400 / 1000;   // 400 ms tone + capture window
  int16_t* tone = (int16_t*)heap_caps_malloc(n * sizeof(int16_t), MALLOC_CAP_SPIRAM);
  int16_t* rec  = (int16_t*)heap_caps_malloc(n * sizeof(int16_t), MALLOC_CAP_SPIRAM);
  if (!tone || !rec) {
    if (tone) heap_caps_free(tone);
    if (rec) heap_caps_free(rec);
    s_busy = false;
    return false;
  }
  for (size_t i = 0; i < n; i++)
    tone[i] = (int16_t)(8000.0f * sinf(2.0f * (float)M_PI * toneHz * (float)i / (float)rate));

  // Open BOTH channels here, sequentially (no concurrent i2s_new_channel). The
  // play task then only writes TX while this task reads RX — real full duplex.
  bool ok = false;
  float mag = 0.0f; uint16_t rr = 0;
  if (micInit() && spkOpen(rate)) {
    micSettle();
    static LbCtx ctx;
    ctx.tone = tone; ctx.n = n; ctx.done = false;
    TaskHandle_t th = nullptr;
    xTaskCreatePinnedToCore(lbPlayTask, "lbplay", 4096, &ctx, 1, &th, 0);
    size_t got = 0; uint32_t t0 = millis();
    while (got < n && millis() - t0 < 700) {
      size_t r = 0;
      if (i2s_channel_read(g_rx, rec + got, (n - got) * sizeof(int16_t), &r, 100) == ESP_OK)
        got += r / sizeof(int16_t);
    }
    uint32_t guard = millis();
    while (!ctx.done && millis() - guard < 500) delay(5);   // let the play task finish
    mag = tone::goertzel(rec, got, rate, toneHz);
    rr  = tone::rms(rec, got);
    ok  = mag > 1000.0f;                     // detection threshold — tune on hardware
  }
  spkClose();
  micDeinit();
  heap_caps_free(tone);
  heap_caps_free(rec);
  if (magOut) *magOut = (uint32_t)mag;
  if (rmsOut) *rmsOut = rr;
  s_busy = false;
  return ok;
}

// ---- lifecycle ---------------------------------------------------------------
bool begin() { return true; }   // channels open lazily on first use
void end() { spkClose(); micDeinit(); }

}  // namespace solide::audio
