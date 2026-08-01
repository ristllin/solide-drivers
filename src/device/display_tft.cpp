#include "solide/display_tft.h"

#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "solide/board.h"
#include "solide/boards/board_solide_s3.h"

// ============================================================================
// ILI9341 over SPI. Deliberately minimal: the caller composes a finished
// RGB565 framebuffer (portable, host-tested), so all this owns is the init
// sequence, the window/blit, and the backlight.
//
// The bus is SHARED with the XPT2046 touch controller (see touch.cpp) — hence
// SPI transactions with per-device settings rather than a single global speed.
// The panel runs at 40 MHz; the touch controller cannot go near that.
//
// ⚠ THREADING: the blit runs on this file's render task while touch is read
// from the main loop, so two tasks drive one bus. That is safe because Arduino's
// spiTransaction()/spiEndTransaction() take a per-bus mutex (esp32-hal-spi.c),
// which serialises the two — verified, not assumed. The visible cost is
// latency, not corruption: a touch read issued mid-blit blocks until the frame
// finishes (~31 ms at 40 MHz), which is far inside the 8 s task watchdog.
// ============================================================================

namespace {

constexpr int8_t TFT_SCK  = solide::kBoardSolideS3.tft.sck;
constexpr int8_t TFT_MOSI = solide::kBoardSolideS3.tft.mosi;
constexpr int8_t TFT_MISO = solide::kBoardSolideS3.tft.miso;
constexpr int8_t TFT_CS   = solide::kBoardSolideS3.tft.cs;
constexpr int8_t TFT_DC   = solide::kBoardSolideS3.tft.dc;
constexpr int8_t TFT_RST  = solide::kBoardSolideS3.tft.rst;
constexpr int8_t TFT_BL   = solide::kBoardSolideS3.tft.bl;

// 40 MHz is the ILI9341's documented write ceiling and what these modules run
// at reliably; MODE0, MSB-first.
// ⚠ Panel SPI clock. 40 MHz is the ILI9341's rated maximum and is fine on a
// PCB — but this panel is wired with jumpers and three bridges made on the
// module (T_CLK/T_DIN/T_DO), which is a long, unterminated, unshielded stub.
// Signal integrity, not the controller, is the limit there.
//
// Symptom when it is too fast, and it is a MISLEADING one: register reads (which
// this driver does at 4 MHz) stay perfect, so the panel looks healthy — MADCTL
// reads back correctly, the render task is alive, the backlight is on, the
// framebuffer holds the right pixels — while the 40 MHz RAMWR burst never lands
// and the glass shows white or black. Every software-side check passes.
// Runtime-tunable so the ceiling can be found on real wiring without a reflash.
// 40 MHz — the ILI9341's rated maximum, and MEASURED sound on this wiring:
// TFTHZ sweeps 64-pixel burst round-trips at 4/10/20/26/40 MHz and TFTFILL?
// round-trips whole RGB frames through the real blit path, reading back the far
// corners. Both report zero mismatches at 40. Running slower "for margin" would
// be cargo cult against evidence, so the shipped clock stays at spec; TFTHZ
// exists to re-measure in seconds if a board ever disagrees.
uint32_t g_panelHz = 40000000;
SPISettings panelSPI() { return SPISettings(g_panelHz, MSBFIRST, SPI_MODE0); }
#define kPanelSPI panelSPI()

// The TFT sits on the same pads the e-paper used (HSPI/SPI3).
SPIClass tftSPI(HSPI);

QueueHandle_t     rq          = nullptr;
volatile bool     g_taskAlive = false;
volatile bool     g_busy      = false;
uint8_t           g_backlight = 100;
bool              g_flip = false;   // which end of the landscape panel is up
bool              g_blAttached = false;  // did the backlight PWM actually attach?

// ILI9341 command set (only what is used).
enum : uint8_t {
  CMD_SWRESET = 0x01, CMD_SLPOUT = 0x11, CMD_DISPON = 0x29,
  CMD_NORON   = 0x13,   // normal display mode ON (exits PARTIAL mode)
  CMD_INVOFF  = 0x20,   // inversion off
  CMD_VSCRSADD = 0x37,  // vertical scroll start address
  CMD_CASET   = 0x2A, CMD_RASET  = 0x2B, CMD_RAMWR  = 0x2C,
  CMD_MADCTL  = 0x36, CMD_PIXFMT = 0x3A,
};

inline void dcCmd()  { digitalWrite(TFT_DC, LOW); }
inline void dcData() { digitalWrite(TFT_DC, HIGH); }

void writeCmd(uint8_t c) {
  dcCmd();
  tftSPI.write(c);
  dcData();
}

void writeCmdData(uint8_t c, const uint8_t* d, size_t n) {
  writeCmd(c);
  for (size_t i = 0; i < n; i++) tftSPI.write(d[i]);
}

// Address window for a full-screen write.
void setFullWindow() {
  const uint8_t cols[4] = {0, 0, uint8_t((solide::display_tft::kW - 1) >> 8),
                           uint8_t((solide::display_tft::kW - 1) & 0xFF)};
  const uint8_t rows[4] = {0, 0, uint8_t((solide::display_tft::kH - 1) >> 8),
                           uint8_t((solide::display_tft::kH - 1) & 0xFF)};
  writeCmdData(CMD_CASET, cols, 4);
  writeCmdData(CMD_RASET, rows, 4);
}

// MADCTL for the mounted orientation. Bit 3 (0x08) is BGR: these red modules are
// BGR-wired, and without it every colour comes out channel-swapped (a blue UI
// renders orange). MV (0x20) is the portrait->landscape rotation; adding MY|MX
// turns the landscape surface through 180 degrees.
uint8_t madctlFor(bool flip) { return flip ? 0xE8 : 0x28; }

void panelInit() {
  // Hardware reset if the pin is fitted, else the software reset alone.
  if (TFT_RST >= 0) {
    pinMode(TFT_RST, OUTPUT);
    digitalWrite(TFT_RST, HIGH); delay(5);
    digitalWrite(TFT_RST, LOW);  delay(20);
    digitalWrite(TFT_RST, HIGH); delay(150);
  }

  tftSPI.beginTransaction(kPanelSPI);
  digitalWrite(TFT_CS, LOW);

  writeCmd(CMD_SWRESET);
  delay(150);

  const uint8_t madctl = madctlFor(g_flip);
  writeCmdData(CMD_MADCTL, &madctl, 1);

  const uint8_t pixfmt = 0x55;   // 16 bit/px, RGB565
  writeCmdData(CMD_PIXFMT, &pixfmt, 1);

  writeCmd(CMD_SLPOUT);
  delay(120);
  writeCmd(CMD_DISPON);
  delay(20);

  digitalWrite(TFT_CS, HIGH);
  tftSPI.endTransaction();
}

// Re-assert ONLY the mode state a panel reset would have lost: colour format,
// memory access order, sleep-out and display-on. Deliberately NOT panelInit():
// that pulses RESET and issues SWRESET, which BLANKS the panel — running it on a
// timer would flash the screen every few seconds. These four commands are
// idempotent, take microseconds, and are invisible when the panel is already fine.
//
// Why this exists: the ILI9341 can lose its state without the ESP32 noticing (a
// brownout on its 3V3 rail, ESD, a glitch on RESET). It then reverts to defaults —
// sleeping, 18-bit pixel format — so the next pixel stream renders as nothing, and
// the panel sits WHITE while the firmware believes it is painted. Observed on
// hardware 2026-07-29.
void panelRearm() {
  tftSPI.beginTransaction(kPanelSPI);
  digitalWrite(TFT_CS, LOW);
  const uint8_t madctl = madctlFor(g_flip);   // same value panelInit sets
  writeCmdData(CMD_MADCTL, &madctl, 1);
  const uint8_t pixfmt = 0x55;   // 16 bit/px RGB565
  writeCmdData(CMD_PIXFMT, &pixfmt, 1);
  // ⚠ Also reset the display MODES, not just sleep/display-on.
  //
  // These are the states that blank or corrupt the picture while leaving MADCTL,
  // GRAM, the render task and the backlight all reading perfectly healthy — the
  // exact signature of the blank-screen fault, and invisible to every diagnostic
  // this driver has (RDDPM is unimplemented on this panel, so display state
  // cannot be read back at all).
  //   NORON     — leaves PARTIAL mode, which shows a sliver and blanks the rest
  //   INVOFF    — leaves inverted colour
  //   VSCRSADD 0— rewinds a vertical scroll that would display the wrong region
  // All idempotent, all invisible on a healthy panel, all microseconds. Since
  // the fault cannot be detected, the recovery has to cover the possibilities
  // blindly rather than wait for a signal that will never come.
  writeCmd(CMD_NORON);
  writeCmd(CMD_INVOFF);
  const uint8_t scroll[2] = {0, 0};
  writeCmdData(CMD_VSCRSADD, scroll, 2);
  writeCmd(CMD_SLPOUT);
  writeCmd(CMD_DISPON);
  digitalWrite(TFT_CS, HIGH);
  tftSPI.endTransaction();
}

void blit(const uint16_t* fb) {
  tftSPI.beginTransaction(kPanelSPI);
  digitalWrite(TFT_CS, LOW);
  setFullWindow();
  writeCmd(CMD_RAMWR);
  // The framebuffer is already big-endian RGB565 — the panel's own wire order —
  // so it goes out as raw bytes with no per-pixel work.
  tftSPI.writeBytes(reinterpret_cast<const uint8_t*>(fb),
                    size_t(solide::display_tft::kW) * solide::display_tft::kH * 2);
  digitalWrite(TFT_CS, HIGH);
  tftSPI.endTransaction();
}

struct FrameCmd { const uint16_t* fb; };

void renderTask(void*) {
  FrameCmd cmd;
  for (;;) {
    if (xQueueReceive(rq, &cmd, portMAX_DELAY) == pdTRUE) {
      blit(cmd.fb);
      g_busy = false;   // cleared only after the pixels are on the panel
    }
  }
}

}  // namespace

namespace solide::display_tft {

bool begin() {
  if (rq) return true;   // idempotent, same contract as display::begin()
  if (TFT_SCK < 0) return false;   // no TFT fitted on this board

  pinMode(TFT_CS, OUTPUT);  digitalWrite(TFT_CS, HIGH);
  pinMode(TFT_DC, OUTPUT);  digitalWrite(TFT_DC, HIGH);

  // MISO is bound because the touch controller shares the bus and reports on
  // it; the panel itself is write-only.
  tftSPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, -1 /*CS handled per-device*/);

  panelInit();

  if (TFT_BL >= 0) {
    // Arduino-ESP32 v3 LEDC API: attach picks the channel itself.
    // ⚠ CHECK the attach. Unchecked, a failure here makes every setBacklight()
    // a silent no-op while backlight() keeps reporting the percentage we asked
    // for — so the diagnostics claim "backlight on" for a panel that is dark.
    // That is the same class of lie as reading the framebuffer and calling it
    // the glass, and it cost real time during the blank-screen investigation.
    g_blAttached = ledcAttach(TFT_BL, 5000 /*Hz*/, 8 /*bit*/);
    if (!g_blAttached) log_e("display_tft: backlight PWM attach FAILED on GPIO %d", int(TFT_BL));
    setBacklight(100);
  }

  rq = xQueueCreate(1, sizeof(FrameCmd));   // depth 1: frames coalesce, never queue up
  if (!rq) {
    log_e("tft: queue create FAILED heap=%u", ESP.getFreeHeap());
    return false;
  }
  if (xTaskCreatePinnedToCore(renderTask, "tft", 4096, nullptr, 1, nullptr, 1) != pdPASS) {
    log_e("tft: task create FAILED heap=%u", ESP.getFreeHeap());
    vQueueDelete(rq);   // no consumer — drop the queue so pushFrame() no-ops
    rq = nullptr;
    return false;
  }
  // Set HERE, not as the task's first statement: the task runs at the same
  // priority on the same core as setup(), so it has not been scheduled yet when
  // begin() returns — taskAlive() would read false on a perfectly healthy board
  // and any self-test row asserting it would false-FAIL.
  g_taskAlive = true;
  return true;
}

bool taskAlive() { return g_taskAlive; }
bool busy()      { return g_busy; }

// Read a panel register over MISO. Diagnostic ONLY: it answers the question that
// decides where the touch fault lives. The panel's SDO and the touch's T_DO are
// bridged onto ONE MISO line on this module, so if the PANEL can be read back the
// line and the pin are electrically fine and the fault is in the touch path; if
// NEITHER can be read, the shared MISO itself is the problem.
// ILI9341 reads need a slow clock (~6 MHz max) and discard one dummy byte.
uint32_t readReg(uint8_t reg, int nbytes) {
  const SPISettings slow(4000000, MSBFIRST, SPI_MODE0);
  tftSPI.beginTransaction(slow);
  digitalWrite(TFT_CS, LOW);
  dcCmd();
  tftSPI.transfer(reg);
  dcData();
  tftSPI.transfer(0x00);              // dummy byte the datasheet requires
  uint32_t v = 0;
  for (int i = 0; i < nbytes; i++) v = (v << 8) | tftSPI.transfer(0x00);
  digitalWrite(TFT_CS, HIGH);
  tftSPI.endTransaction();
  return v;
}

// Hold the panel in hardware RESET. Diagnostic ONLY: a reset ILI9341 releases its
// SDO pin, so reading the touch controller while this is asserted isolates the two
// devices on the shared MISO line. If touch data appears only here, the panel is
// not tri-stating and the bridge is a hardware conflict, not a firmware bug.
// The caller MUST follow with reinit() — the panel is dead until it does.
void holdReset(bool asserted) {
  if (TFT_RST < 0) return;
  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, asserted ? LOW : HIGH);
  delay(asserted ? 10 : 150);
}

void reinit() { panelInit(); }

// Is the panel still configured, or has it silently reset?
//
// RDDST's top byte mirrors MADCTL, and we KNOW what we wrote — so comparing it
// against madctlFor() is a deterministic check, not a heuristic. A panel that
// lost its state reverts to the 0x00 power-on default and fails this instantly.
// Measured on hardware: 0x28 stable across reads on a healthy panel (bit 0 is
// the scan-direction flag and toggles during refresh, hence the mask).
//
// ⚠ RDDPM (0x0A) would have been the obvious register, but it reads 0x00 on this
// panel even when it is demonstrably working — unusable. RDDST does work.
bool healthy() {
  const uint8_t got = uint8_t(readReg(0x09, 4) >> 24);
  return (got & 0xFE) == (madctlFor(g_flip) & 0xFE);
}

// Write a known pixel pattern at the CURRENT clock, then read it back at a slow,
// known-good clock and count mismatches. This is the objective test for the
// pixel path: it needs no human looking at the glass, and it separates "the
// panel is misconfigured" (registers, which readReg already covers) from "the
// pixel burst does not survive the wiring".
//
// ⚠ The readback is deliberately SLOW and the write is not. RAMRD tops out
// around 6 MHz on this controller, so reading fast would measure the read path
// instead of the thing under test. And RAMRD returns 18-bit 6-6-6 regardless of
// the 16-bit write format, so the comparison is on the top 5 bits per channel.
int pixelRoundTrip(int n) {
  if (n < 1) n = 1;
  if (n > 64) n = 64;
  uint16_t want[64];
  for (int i = 0; i < n; i++) want[i] = uint16_t((i * 2731) ^ 0xA5A5);   // varied bit patterns

  // ---- write at the clock under test ----
  tftSPI.beginTransaction(kPanelSPI);
  digitalWrite(TFT_CS, LOW);
  const uint8_t cols[4] = {0, 0, uint8_t((n - 1) >> 8), uint8_t((n - 1) & 0xFF)};
  const uint8_t rows[4] = {0, 0, 0, 0};
  writeCmdData(CMD_CASET, cols, 4);
  writeCmdData(CMD_RASET, rows, 4);
  writeCmd(CMD_RAMWR);
  dcData();
  // ⚠ writeBytes, NOT a loop of transfer(). Per-byte transfers leave gaps between
  // bytes, so they do not produce the sustained burst a real blit does — an
  // earlier version of this test used them and reported a clean 40 MHz while the
  // actual 153 KB blit was the thing under suspicion. Measure the path you ship.
  uint8_t burst[128];
  for (int i = 0; i < n; i++) {
    burst[i * 2]     = uint8_t(want[i] >> 8);
    burst[i * 2 + 1] = uint8_t(want[i] & 0xFF);
  }
  tftSPI.writeBytes(burst, size_t(n) * 2);
  digitalWrite(TFT_CS, HIGH);
  tftSPI.endTransaction();

  // ---- read back slowly ----
  const SPISettings slow(4000000, MSBFIRST, SPI_MODE0);
  tftSPI.beginTransaction(slow);
  digitalWrite(TFT_CS, LOW);
  writeCmdData(CMD_CASET, cols, 4);
  writeCmdData(CMD_RASET, rows, 4);
  dcCmd();
  tftSPI.transfer(0x2E);            // RAMRD
  dcData();
  tftSPI.transfer(0x00);            // dummy byte
  int bad = 0;
  for (int i = 0; i < n; i++) {
    const uint8_t r = tftSPI.transfer(0x00);
    const uint8_t g = tftSPI.transfer(0x00);
    const uint8_t b = tftSPI.transfer(0x00);
    const uint16_t got = uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    if (got != want[i]) bad++;
  }
  digitalWrite(TFT_CS, HIGH);
  tftSPI.endTransaction();
  return bad;
}

// Read one pixel back from anywhere on the panel (slow clock — RAMRD is slow).
// Used to verify the FULL-FRAME path: the 64-pixel round-trip above only proves
// the origin works, and an addressing fault would pass it while leaving most of
// the screen untouched.
uint16_t readPixel(int px, int py) {
  const SPISettings slow(4000000, MSBFIRST, SPI_MODE0);
  tftSPI.beginTransaction(slow);
  digitalWrite(TFT_CS, LOW);
  const uint8_t cols[4] = {uint8_t(px >> 8), uint8_t(px & 0xFF),
                           uint8_t(px >> 8), uint8_t(px & 0xFF)};
  const uint8_t rows[4] = {uint8_t(py >> 8), uint8_t(py & 0xFF),
                           uint8_t(py >> 8), uint8_t(py & 0xFF)};
  writeCmdData(CMD_CASET, cols, 4);
  writeCmdData(CMD_RASET, rows, 4);
  dcCmd();
  tftSPI.transfer(0x2E);
  dcData();
  tftSPI.transfer(0x00);
  const uint8_t r = tftSPI.transfer(0x00);
  const uint8_t g = tftSPI.transfer(0x00);
  const uint8_t b = tftSPI.transfer(0x00);
  digitalWrite(TFT_CS, HIGH);
  tftSPI.endTransaction();
  return uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint32_t panelHz() { return g_panelHz; }
void setPanelHz(uint32_t hz) {
  if (hz < 1000000) hz = 1000000;
  if (hz > 40000000) hz = 40000000;
  g_panelHz = hz;
}

// Blit a PSRAM framebuffer through an INTERNAL bounce buffer, a band at a time.
//
// ⚠ Why this exists: a 150 KB DMA burst sourced directly from PSRAM was measured
// to RESET this panel — MADCTL 0x28 -> 0x00 — with nothing else running on the
// board. fill(), which sources from internal memory, never does. On the S3, PSRAM
// is reached over the same external-memory bus the SPI DMA must arbitrate for, so
// a long burst out of PSRAM is a materially different transaction from one out of
// internal SRAM. Copying band-by-band into a small internal DMA-capable buffer
// keeps every panel command and the total byte count identical and changes ONLY
// the source memory.
//
// Cost is one extra memcpy of the frame; the SPI time dominates either way.
bool pushFrameChunked(const uint16_t* fb, uint16_t* bounce, int rowsPerChunk) {
  if (!fb || !bounce || rowsPerChunk < 1) return false;
  if (!rq) return false;
  tftSPI.beginTransaction(kPanelSPI);
  digitalWrite(TFT_CS, LOW);
  setFullWindow();
  writeCmd(CMD_RAMWR);
  const int W = solide::display_tft::kW, H = solide::display_tft::kH;
  for (int y = 0; y < H; y += rowsPerChunk) {
    const int rows = (y + rowsPerChunk > H) ? (H - y) : rowsPerChunk;
    const size_t n = size_t(rows) * size_t(W);
    memcpy(bounce, fb + size_t(y) * size_t(W), n * 2);
    tftSPI.writeBytes(reinterpret_cast<const uint8_t*>(bounce), n * 2);
  }
  digitalWrite(TFT_CS, HIGH);
  tftSPI.endTransaction();
  return true;
}

void rearm() { panelRearm(); }

void setFlip(bool upsideDown) {
  if (g_flip == upsideDown) return;
  g_flip = upsideDown;
  // MADCTL alone — no reset, so this is instant and does not blank the panel. The
  // next full frame lands in the new orientation.
  if (rq) panelRearm();
}

bool flipped() { return g_flip; }

bool pushFrame(const uint16_t* fb) {
  if (!rq || !fb) return false;
  if (g_busy) return false;   // previous frame still going out; caller retries
  g_busy = true;
  FrameCmd c{fb};
  if (xQueueSend(rq, &c, 0) != pdTRUE) {
    g_busy = false;
    return false;
  }
  return true;
}

void setBacklight(uint8_t pct) {
  if (pct > 100) pct = 100;
  g_backlight = pct;
  if (TFT_BL < 0) return;                 // tied to 3V3: always on, nothing to do
  if (!g_blAttached) return;              // no PWM channel — do not pretend
  ledcWrite(TFT_BL, (uint32_t(pct) * 255) / 100);
}

// Whether the backlight is actually CONTROLLABLE, as opposed to the percentage
// we last asked for. A dark panel with backlight()==100 and this false means the
// light was never driven at all.
bool backlightAttached() { return TFT_BL < 0 || g_blAttached; }

uint8_t backlight() { return g_backlight; }

void fill(uint16_t colour565) {
  if (!rq) return;
  // Wait out any frame already going to the panel. The SPI HAL serialises the
  // two transactions so the BUS is safe either way, but two full-screen writes
  // interleaving at transaction granularity would put half of each on the
  // panel — a visibly torn frame rather than a clean fill.
  while (g_busy) delay(1);

  // Swap to the panel's big-endian wire order, then push one row at a time so a
  // solid fill needs a 480-byte scratch instead of a second 150 KB buffer.
  const uint16_t be = uint16_t((colour565 << 8) | (colour565 >> 8));
  static uint16_t row[kW];
  for (int i = 0; i < kW; i++) row[i] = be;

  tftSPI.beginTransaction(kPanelSPI);
  digitalWrite(TFT_CS, LOW);
  setFullWindow();
  writeCmd(CMD_RAMWR);
  for (int y = 0; y < kH; y++)
    tftSPI.writeBytes(reinterpret_cast<const uint8_t*>(row), size_t(kW) * 2);
  digitalWrite(TFT_CS, HIGH);
  tftSPI.endTransaction();
}

void clear() { fill(0x0000); }

SPIClass* bus() { return &tftSPI; }

}  // namespace solide::display_tft
