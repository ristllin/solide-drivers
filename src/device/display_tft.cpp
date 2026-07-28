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
const SPISettings kPanelSPI(40000000, MSBFIRST, SPI_MODE0);

// The TFT sits on the same pads the e-paper used (HSPI/SPI3).
SPIClass tftSPI(HSPI);

QueueHandle_t     rq          = nullptr;
volatile bool     g_taskAlive = false;
volatile bool     g_busy      = false;
uint8_t           g_backlight = 100;

// ILI9341 command set (only what is used).
enum : uint8_t {
  CMD_SWRESET = 0x01, CMD_SLPOUT = 0x11, CMD_DISPON = 0x29,
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

  // MADCTL: portrait, BGR panel order. Bit3 (BGR) is set because these red
  // boards are BGR-wired — without it every colour comes out channel-swapped
  // (a blue UI renders orange), which is the classic "wrong colours" symptom.
  const uint8_t madctl = 0x48;
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
    ledcAttach(TFT_BL, 5000 /*Hz*/, 8 /*bit*/);
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
  ledcWrite(TFT_BL, (uint32_t(pct) * 255) / 100);
}

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
