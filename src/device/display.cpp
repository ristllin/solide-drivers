#include "solide/display.h"
#include "solide/boards/board_solide_s3.h"
#include "solide/status_art.h"
#include "esp_heap_caps.h"

#include <SPI.h>
#include <vector>
#include <GxEPD2_3C.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>

// ============================================================================
// WeAct 2.9" 3-colour (SSD1680) driver on a DEDICATED HSPI/SPI3 bus (separate
// from the SD card on FSPI/SPI2). Pins from the canonical board config. Two
// GxEPD2 instances share one panel: a fast-B/W subclass (custom WS_20_30 LUT,
// ~2.2 s crisp black) and the 3-colour driver (OTP, ~18.5 s). All access runs on
// a render task fed by an 8-entry queue.
//
// Extracted from the original firmware's display.cpp; the WS_20_30 waveform, the
// GxEPD2_290_C90fast subclass, the word-wrap, and the render machinery are
// preserved byte-for-byte. The app/branding "status screen" was removed — this
// layer exposes only generic primitives (requestText / requestBitmap / requestMenu).
// ============================================================================

// E-paper pins from the canonical board config.
static constexpr int EPD_SCK  = solide::kBoardSolideS3.epd.sck;
static constexpr int EPD_MOSI = solide::kBoardSolideS3.epd.mosi;
static constexpr int EPD_CS   = solide::kBoardSolideS3.epd.cs;
static constexpr int EPD_DC   = solide::kBoardSolideS3.epd.dc;
static constexpr int EPD_RST  = solide::kBoardSolideS3.epd.rst;
static constexpr int EPD_BUSY = solide::kBoardSolideS3.epd.busy;

// Dedicated SPI bus for the e-paper (HSPI/SPI3), separate from the SD card.
static SPIClass    epdSPI(HSPI);
static SPISettings epdSPISettings(4000000, MSBFIRST, SPI_MODE0);

// ---- fast B/W driver: WS_20_30 custom LUT via SSD1680 register path ----------
// Subclasses the T94_V2 B/W driver. Full refresh loads the Waveshare WS_20_30
// waveform (~2-4 s, crisp solid black) instead of the OTP waveform. Partial
// refresh falls through to the parent (unchanged). Bytes verified from Waveshare.
class GxEPD2_290_C90fast : public GxEPD2_290_T94_V2 {
 public:
  GxEPD2_290_C90fast(int16_t cs, int16_t dc, int16_t rst, int16_t busy)
      : GxEPD2_290_T94_V2(cs, dc, rst, busy) {}

  // When set for one render, use the panel's TRUE full-update waveform (the parent's
  // refresh) instead of our fast custom LUT — the only path that actually CLEARS
  // accumulated SSD1680 ghosting. The caller sets it around a firstPage/nextPage.
  bool forceFullUpdate = false;

  void refresh(bool partial_update_mode = false) override {
    if (partial_update_mode) { GxEPD2_290_T94_V2::refresh(true); return; }
    if (forceFullUpdate) {
      // Ghost-clear: the panel's TRUE full-update (OTP waveform) runs noticeably
      // longer than our 2.2 s fast LUT and was hitting GxEPD2's default 10 s
      // _busy_timeout (observed 10001086 us = the cap exactly). Bailing at the cap
      // mid-refresh can leave the ghost only half-wiped, defeating the point. Give
      // the full waveform real headroom for the duration of this one refresh, then
      // restore the default so a stuck panel still can't hang the render task.
      const uint32_t savedTimeout = _busy_timeout;
      _busy_timeout = 20000000UL;   // 20 s, this refresh only
      GxEPD2_290_T94_V2::refresh(false);
      _busy_timeout = savedTimeout;
      return;
    }
    _fastFull();
    _initial_refresh = false;
  }
  void refresh(int16_t x, int16_t y, int16_t w, int16_t h) override {
    GxEPD2_290_T94_V2::refresh(x, y, w, h);
  }

 private:
  void _fastFull() {
    _writeCommand(0x32);
    _writeDataPGM(WS_20_30, 153);
    _waitWhileBusy("fastFull:lut", 100);
    _writeCommand(0x3F); _writeData(pgm_read_byte(&WS_20_30[153]));
    _writeCommand(0x03); _writeData(pgm_read_byte(&WS_20_30[154]));
    _writeCommand(0x04);
    _writeData(pgm_read_byte(&WS_20_30[155]));
    _writeData(pgm_read_byte(&WS_20_30[156]));
    _writeData(pgm_read_byte(&WS_20_30[157]));
    _writeCommand(0x2C); _writeData(pgm_read_byte(&WS_20_30[158]));
    _writeCommand(0x22); _writeData(0xC7);  // register LUT, not OTP
    _writeCommand(0x20);
    _waitWhileBusy("fastFull", full_refresh_time);
    _power_is_on = false;
    _using_partial_mode = false;
  }

 public:
  // Clear the panel's RED RAM (0x26) to white. This is a 3-colour (B/W/RED) SSD1680:
  // the fast B/W path only writes the B/W RAM (0x24) and merges red->black, but a
  // COLOUR render (colorDisp) leaves real data in the red RAM. The ghost-clear uses
  // the panel's TRUE full-update (OTP) waveform, which renders BOTH RAMs — so a stale
  // red plane paints RED and stays (the "screen goes red after a refresh" field bug).
  // Clearing red RAM to 0xFF on every B/W-mode entry guarantees B/W renders (incl.
  // ghost-clears) never resurrect a red plane. Writes the whole RAM, so it's correct
  // regardless of rotation. ~4.7 KB over SPI, only on a colour<->B/W transition.
  void clearRedRAM() {
    const uint16_t wb = (WIDTH + 7) / 8;           // bytes per row (16 for 128 px)
    _writeCommand(0x11); _writeData(0x03);         // data entry: X+ Y+
    _writeCommand(0x44); _writeData(0x00); _writeData((uint8_t)(wb - 1));
    _writeCommand(0x45); _writeData(0x00); _writeData(0x00);
                         _writeData((uint8_t)((HEIGHT - 1) & 0xFF));
                         _writeData((uint8_t)((HEIGHT - 1) >> 8));
    _writeCommand(0x4E); _writeData(0x00);
    _writeCommand(0x4F); _writeData(0x00); _writeData(0x00);
    _writeCommand(0x26);                            // Write RAM (RED); 0xFF = white
    for (uint32_t i = 0; i < (uint32_t)wb * HEIGHT; i++) _writeData(0xFF);
  }

 private:
  static const unsigned char WS_20_30[159];
};

const unsigned char GxEPD2_290_C90fast::WS_20_30[159] PROGMEM = {
  0x80, 0x66, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x40, 0x0, 0x0, 0x0,
  0x10, 0x66, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x20, 0x0, 0x0, 0x0,
  0x80, 0x66, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x40, 0x0, 0x0, 0x0,
  0x10, 0x66, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x20, 0x0, 0x0, 0x0,
  0x0,  0x0,  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  0x0, 0x0, 0x0,
  0x14, 0x8, 0x0, 0x0, 0x0, 0x0, 0x1,
  0xA,  0xA, 0x0, 0xA, 0xA, 0x0, 0x1,
  0x0,  0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0,  0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0,  0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0,  0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0,  0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0,  0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x14, 0x8, 0x0, 0x1, 0x0, 0x0, 0x1,
  0x0,  0x0, 0x0, 0x0, 0x0, 0x0, 0x1,
  0x0,  0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0,  0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x0, 0x0, 0x0,
  0x22, 0x17, 0x41, 0x0, 0x32, 0x36
};

// ---- two display instances on the same physical panel -----------------------
static GxEPD2_BW<GxEPD2_290_C90fast, GxEPD2_290_C90fast::HEIGHT>
    bwDisp(GxEPD2_290_C90fast(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
static GxEPD2_3C<GxEPD2_290_C90c, GxEPD2_290_C90c::HEIGHT>
    colorDisp(GxEPD2_290_C90c(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

static const int16_t SCR_W = 296;
static const int16_t SCR_H = 128;

static volatile int  g_maxScroll = 0;   // max scroll of the last text block
static volatile bool g_taskAlive = false;

// Only re-init on a mode switch (avoids a ~50 ms panel reset per render).
static enum { MODE_NONE, MODE_BW, MODE_COLOR } activeMode = MODE_NONE;

static void ensureBW() {
  if (activeMode == MODE_BW) return;
  bwDisp.init(115200, true, 50, false, epdSPI, epdSPISettings);
  bwDisp.setRotation(1);
  bwDisp.setTextWrap(false);
  // 3-colour panel: wipe any red plane a prior colour render left behind, so a B/W
  // ghost-clear (true full-update) can't resurrect it as a stuck-red screen.
  bwDisp.epd2.clearRedRAM();
  activeMode = MODE_BW;
}
static void ensureColor() {
  if (activeMode == MODE_COLOR) return;
  colorDisp.init(115200, true, 50, false, epdSPI, epdSPISettings);
  colorDisp.setRotation(1);
  colorDisp.setTextWrap(false);
  activeMode = MODE_COLOR;
}

// ---- render queue ------------------------------------------------------------
enum RType { R_TEXT, R_MENU, R_MENU_FULL, R_BITMAP, R_CLEAR };
struct RenderCmd {
  RType type;
  char* a; char* b;
  int16_t extra; bool flag; bool flag2;   // flag2 = fullClear for R_BITMAP
  const uint8_t* bmpK; const uint8_t* bmpR; int16_t w; int16_t h;
};
static QueueHandle_t rq = nullptr;

// ---- word wrap (measurement uses bwDisp; font metrics are driver-independent)
static void appendWord(std::vector<String>& out, String& line, const String& word, int16_t maxW) {
  if (word.length() == 0) return;
  int16_t x1, y1; uint16_t w, h;
  String trial = line.length() ? line + " " + word : word;
  bwDisp.getTextBounds(trial, 0, 0, &x1, &y1, &w, &h);
  if (w <= maxW) { line = trial; return; }
  if (line.length()) { out.push_back(line); line = ""; }
  String rest = word;
  while (rest.length()) {
    bwDisp.getTextBounds(rest, 0, 0, &x1, &y1, &w, &h);
    if (w <= maxW) { line = rest; return; }
    int lo = 1, hi = rest.length(), best = 1;
    while (lo <= hi) {
      int mid = (lo + hi) / 2;
      bwDisp.getTextBounds(rest.substring(0, mid), 0, 0, &x1, &y1, &w, &h);
      if (w <= maxW) { best = mid; lo = mid + 1; } else hi = mid - 1;
    }
    out.push_back(rest.substring(0, best));
    rest = rest.substring(best);
  }
}

static void wrapText(const String& text, int16_t maxW, std::vector<String>& out) {
  String line = "", word = "";
  for (size_t i = 0; i <= text.length(); i++) {
    char c = (i < text.length()) ? text[i] : '\n';
    unsigned char uc = (unsigned char)c;
    // Drop bytes the ASCII GFX font can't render — UTF-8 high bytes (smart quotes,
    // em-dashes, emoji) and stray control chars; else a glyph lookup indexes past
    // the font table -> invalid read -> device reset.
    if (uc > 126) continue;
    if (uc < 32 && c != '\n' && c != '\t') continue;
    if (c == ' ' || c == '\n' || c == '\t') {
      appendWord(out, line, word, maxW);
      word = "";
      if (c == '\n') { out.push_back(line); line = ""; }
    } else {
      word += c;
    }
  }
}

// ---- renderers ---------------------------------------------------------------

// A titled text block — fast B/W: bold title (up to 2 lines) + wrapped body,
// optional knob-scroll with a "[ click: return ]" footer.
static void renderText(const String& title, const String& body,
                       int scrollOffset, bool scrollMode) {
  ensureBW();
  bwDisp.setFullWindow();
  bwDisp.firstPage();
  do {
    bwDisp.fillScreen(GxEPD_WHITE);

    bwDisp.setFont(&FreeSansBold9pt7b);
    bwDisp.setTextColor(GxEPD_BLACK);
    std::vector<String> plines;
    wrapText(title, SCR_W - 4, plines);
    int16_t y = 16;
    for (int i = 0; i < (int)plines.size() && i < 2; i++) {
      bwDisp.setCursor(2, y); bwDisp.print(plines[i]); y += 17;
    }
    y += 4;

    bwDisp.setFont(&FreeSans9pt7b);
    const int16_t lh = 16;
    const int16_t footerY = SCR_H - 4;
    const int16_t bodyEnd = scrollMode ? footerY - lh : SCR_H - 2;

    std::vector<String> alines;
    wrapText(body, SCR_W - 4, alines);
    int total = (int)alines.size();
    int fit = 0;
    for (int16_t yy = y; yy + lh <= bodyEnd; yy += lh) fit++;
    g_maxScroll = total > fit ? total - fit : 0;
    int shown = 0;
    for (int i = scrollOffset; i < total; i++) {
      if (y + lh > bodyEnd) {
        if (!scrollMode) { bwDisp.setCursor(2, y); bwDisp.print("...(more)"); }
        break;
      }
      bwDisp.setCursor(2, y); bwDisp.print(alines[i]); y += lh; shown++;
    }
    if (scrollMode) {
      bwDisp.drawFastHLine(0, footerY - lh - 2, SCR_W, GxEPD_BLACK);
      bwDisp.setCursor(2, footerY);
      bool atEnd = (scrollOffset + shown >= total);
      bwDisp.print(atEnd ? "[ click: return ]" : "v more...");
    }
  } while (bwDisp.nextPage());
}

// Menu — fast B/W: bold title + items; selected row inverted. Packed string is
// "<selected>\x1f<title>\x1f<item0>\x1f<item1>...".
static void renderMenu(const String& packed, bool full) {
  ensureBW();
  std::vector<String> tok;
  int start = 0;
  for (int i = 0; i <= (int)packed.length(); i++) {
    if (i == (int)packed.length() || packed[i] == '\x1f') {
      tok.push_back(packed.substring(start, i));
      start = i + 1;
    }
  }
  int sel = tok.size() > 0 ? tok[0].toInt() : 0;
  String title = tok.size() > 1 ? tok[1] : "";
  static int s_partials = 0;
  bool doFull = full || (++s_partials % 10 == 0);
  if (doFull) bwDisp.setFullWindow();
  else        bwDisp.setPartialWindow(0, 0, SCR_W, SCR_H);
  bwDisp.firstPage();
  do {
    bwDisp.fillScreen(GxEPD_WHITE);
    bwDisp.setFont(&FreeSansBold9pt7b);
    bwDisp.setTextColor(GxEPD_BLACK);
    int total = (int)tok.size() - 2;
    const int rows = 5;
    int first = (sel >= rows) ? sel - rows + 1 : 0;
    if (first > total - rows) first = total - rows;
    if (first < 0) first = 0;
    bwDisp.setCursor(2, 16);
    if (total > 0) bwDisp.print(title + "  " + String(sel + 1) + "/" + String(total));
    else           bwDisp.print(title);
    bwDisp.setFont(&FreeSans9pt7b);
    int16_t y = 36, lh = 17;
    for (int idx = first; idx < total && idx < first + rows; idx++) {
      if (idx == sel) {
        bwDisp.fillRect(0, y - 13, SCR_W, lh, GxEPD_BLACK);
        bwDisp.setTextColor(GxEPD_WHITE);
      } else {
        bwDisp.setTextColor(GxEPD_BLACK);
      }
      bwDisp.setCursor(6, y); bwDisp.print(tok[idx + 2]); y += lh;
    }
  } while (bwDisp.nextPage());
}

// Full-screen bitmap: black plane always black; red plane red (3-colour) or
// merged to black (fast B/W).
static void renderBitmap(const uint8_t* black, const uint8_t* red, int16_t w, int16_t h,
                         bool fast, bool fullClear = false) {
  if (fast) {
    ensureBW();
    // fullClear: render this frame with the TRUE full-update waveform (slower, but
    // the only thing that wipes accumulated ghosting). The scheduler asks for it
    // every FullRefreshEveryN renders + on the long-idle failsafe.
    bwDisp.epd2.forceFullUpdate = fullClear;
    bwDisp.setFullWindow();
    bwDisp.firstPage();
    do {
      bwDisp.fillScreen(GxEPD_WHITE);
      if (black) bwDisp.drawBitmap(0, 0, black, w, h, GxEPD_BLACK);
      if (red)   bwDisp.drawBitmap(0, 0, red,   w, h, GxEPD_BLACK);
    } while (bwDisp.nextPage());
    bwDisp.epd2.forceFullUpdate = false;
  } else {
    ensureColor();
    colorDisp.setFullWindow();
    colorDisp.firstPage();
    do {
      colorDisp.fillScreen(GxEPD_WHITE);
      if (black) colorDisp.drawBitmap(0, 0, black, w, h, GxEPD_BLACK);
      if (red)   colorDisp.drawBitmap(0, 0, red,   w, h, GxEPD_RED);
    } while (colorDisp.nextPage());
  }
}

static void renderClear() {
  ensureBW();
  bwDisp.setFullWindow();
  bwDisp.firstPage();
  do { bwDisp.fillScreen(GxEPD_WHITE); } while (bwDisp.nextPage());
}

// ---- render task (pinned to core 1) -----------------------------------------
static void renderTask(void*) {
  g_taskAlive = true;
  RenderCmd cmd;
  for (;;) {
    if (xQueueReceive(rq, &cmd, portMAX_DELAY) == pdTRUE) {
      String a = cmd.a ? String(cmd.a) : String("");
      String b = cmd.b ? String(cmd.b) : String("");
      if (cmd.a) free(cmd.a);
      if (cmd.b) free(cmd.b);
      switch (cmd.type) {
        case R_TEXT:      renderText(a, b, cmd.extra, cmd.flag); break;
        case R_MENU:      renderMenu(a, false); break;
        case R_MENU_FULL: renderMenu(a, true);  break;
        case R_BITMAP:    renderBitmap(cmd.bmpK, cmd.bmpR, cmd.w, cmd.h, cmd.flag, cmd.flag2); break;
        case R_CLEAR:     renderClear(); break;
      }
    }
  }
}

static void enqueue(RType t, const String* a, const String* b, int16_t extra = 0, bool flag = false,
                    const uint8_t* bK = nullptr, const uint8_t* bR = nullptr, int16_t w = 0, int16_t h = 0,
                    bool flag2 = false) {
  if (!rq) return;
  RenderCmd c;
  c.type = t;
  c.a = a ? strdup(a->c_str()) : nullptr;
  c.b = b ? strdup(b->c_str()) : nullptr;
  c.extra = extra; c.flag = flag; c.flag2 = flag2;
  c.bmpK = bK; c.bmpR = bR; c.w = w; c.h = h;
  if (xQueueSend(rq, &c, 0) != pdTRUE) {
    if (c.a) free(c.a);
    if (c.b) free(c.b);
  }
}

namespace solide::display {

bool begin() {
  if (rq) return true;   // idempotent — a second call is a safe no-op (queue exists)
  // Bind the dedicated HSPI/SPI3 bus BEFORE the first init(). MISO is unused (-1):
  // the panel is write-only.
  epdSPI.begin(EPD_SCK, -1 /*MISO*/, EPD_MOSI, EPD_CS);
  bwDisp.init(115200, true, 50, false, epdSPI, epdSPISettings);
  bwDisp.setRotation(1);
  bwDisp.setTextWrap(false);
  activeMode = MODE_BW;
  rq = xQueueCreate(8, sizeof(RenderCmd));
  if (!rq) {
    log_e("display: queue create FAILED heap=%u max8=%u", ESP.getFreeHeap(),
          heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    return false;
  }
  BaseType_t ok = xTaskCreatePinnedToCore(renderTask, "epd", 8192, nullptr, 1, nullptr, 1);
  if (ok != pdPASS) {
    log_e("display: task create FAILED heap=%u max8=%u", ESP.getFreeHeap(),
          heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    vQueueDelete(rq);   // no consumer task — free the queue so enqueue() no-ops
    rq = nullptr;
    return false;
  }
  return true;
}

bool taskAlive() { return g_taskAlive; }

void requestText(const String& title, const String& body, int scrollOffset, bool scrollMode) {
  enqueue(R_TEXT, &title, &body, (int16_t)scrollOffset, scrollMode);
}
int maxTextScroll() { return g_maxScroll; }

void requestMenu(const solide::menu::MenuView& v, bool full) {
  String s = String(v.selected);
  s += '\x1f'; s += v.title.c_str();
  for (const auto& it : v.items) { s += '\x1f'; s += it.c_str(); }
  enqueue(full ? R_MENU_FULL : R_MENU, &s, nullptr);
}

void requestBitmap(const uint8_t* black, const uint8_t* red, int16_t w, int16_t h, bool fast,
                   bool fullClear) {
  enqueue(R_BITMAP, nullptr, nullptr, 0, fast, black, red, w, h, fullClear);
}

void showArt(int state, bool fast) {
  if (state < 0 || state >= solide::art::COUNT) return;
  const solide::art::Art& art = solide::art::ART[state];
  requestBitmap(art.black, art.red, solide::art::WIDTH, solide::art::HEIGHT, fast);
}

void clear() { enqueue(R_CLEAR, nullptr, nullptr); }

}  // namespace solide::display
