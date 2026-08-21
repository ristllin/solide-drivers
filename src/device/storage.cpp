#include "solide/storage.h"
#include <SPI.h>
#include <SD.h>
#include <SD_MMC.h>
#include <FS.h>
#include "solide/boards/active_board.h"

// SD bus pins from the canonical board config (FSPI/SPI2 native IOMUX).
static constexpr int SD_CS   = solide::activeBoard().sd.cs;
static constexpr int SD_MOSI = solide::activeBoard().sd.mosi;
static constexpr int SD_SCK  = solide::activeBoard().sd.sck;
static constexpr int SD_MISO = solide::activeBoard().sd.miso;

// Two card backends selected by board data (sdKind), never by the caller:
//   Spi   - microSD over the dedicated FSPI/SPI2 bus (Solide S3). Init sequence
//           is the one validated in the bring-up validator (main.cpp):
//             SPIClass(FSPI).begin(SCK, MISO, MOSI, CS)  then  SD.begin(CS, spi)
//   Sdmmc - on-board SDMMC/SDIO 4-bit slot (Freenove). SD_MMC.setPins(...) +
//           SD_MMC.begin(mount, 1bit=false). Same fs::FS surface downstream.
// The shared file ops go through card() so the rest of the file is backend-blind.

namespace solide::storage {

static constexpr bool kSdmmc = solide::activeBoard().sdKind == solide::SdKind::Sdmmc;
static SPIClass g_spi(FSPI);
static bool     g_ok = false;

static const uint64_t MB = 1024ULL * 1024ULL;

// The mounted filesystem, chosen at compile time. if constexpr keeps the unused
// backend's global out of ODR use, so a Spi board never drags in SD_MMC (and v.v.).
static fs::FS& card() {
  if constexpr (kSdmmc) return SD_MMC; else return SD;
}
// card-type/size accessors also via if constexpr, so the unused backend's
// SD/SD_MMC methods are never ODR-used and a Spi board doesn't link SD_MMC.
// cardType() is public (declared in storage.h) - diagnostics need it even when
// begin() failed; the others stay internal, surfaced via cardSizeMB/usedMB/freeMB.
uint8_t cardType()          { if constexpr (kSdmmc) return SD_MMC.cardType();  else return SD.cardType(); }
static uint64_t cardBytes() { if constexpr (kSdmmc) return SD_MMC.cardSize();  else return SD.cardSize(); }
static uint64_t usedBytes() { if constexpr (kSdmmc) return SD_MMC.usedBytes(); else return SD.usedBytes(); }

// Public accessor (storage.h) for callers that need to route their OWN file I/O
// through the mounted card rather than this module's writeFile/readFile.
fs::FS& activeFs() { return card(); }

// ---- mount ----------------------------------------------------------------
bool begin() {
  if (g_ok) return true;   // already mounted - idempotent

  if constexpr (kSdmmc) {
    const auto& sd = solide::activeBoard().sdmmc;
    // 4-bit SDMMC. setPins must precede begin(); 1bit=false selects the 4-bit bus.
    if (!SD_MMC.setPins(sd.clk, sd.cmd, sd.d0, sd.d1, sd.d2, sd.d3)) {
      Serial.println("SD: SDMMC setPins failed (running without SD)");
      g_ok = false;
      return false;
    }
    if (!SD_MMC.begin("/sdcard", false /*1-bit? no, 4-bit*/, false /*format*/)) {
      Serial.println("SD: SDMMC begin() failed - check card inserted/FAT32, 3V3. "
                     "(running without SD)");
      g_ok = false;
      return false;
    }
  } else {
    g_spi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS, g_spi)) {
      Serial.println("SD: begin() failed - check CS=10 MOSI=11 CLK=12 MISO=13, "
                     "3V3 power, card inserted/FAT32. (running without SD)");
      g_ok = false;
      return false;
    }
  }

  const uint8_t ct = cardType();
  if (ct == CARD_NONE) {
    Serial.println("SD: no card detected (running without SD)");
    end();
    g_ok = false;
    return false;
  }
  const char* tn = ct == CARD_MMC  ? "MMC"
                 : ct == CARD_SD   ? "SDSC"
                 : ct == CARD_SDHC ? "SDHC"
                 : "UNKNOWN";
  const uint64_t sz = cardBytes();
  const uint64_t us = usedBytes();
  Serial.printf("SD: %s, %llu MB total, %llu MB used\n", tn, sz / MB, us / MB);

  g_ok = true;
  return true;
}

bool available() { return g_ok; }

// ---- unmount --------------------------------------------------------------
void end() {
  // Always tear the FS + bus down, even if g_ok was already false: a card that
  // was pulled leaves the driver state half-alive, and a bare begin() then
  // returns stale success. end() forces a clean re-probe next begin(). It is
  // idempotent and safe with no card.
  if constexpr (kSdmmc) {
    SD_MMC.end();
  } else {
    SD.end();
    g_spi.end();
  }
  g_ok = false;
}

// ---- helpers --------------------------------------------------------------
// Create every parent dir of `path` (mkdir is a no-op if it already exists).
// "/logs/sub/boot.txt" → mkdir("/logs"), mkdir("/logs/sub").
static void ensureParentDirs(const char* path) {
  if (!path || path[0] != '/') return;
  String p(path);
  int slash = p.indexOf('/', 1);
  while (slash > 0) {
    String dir = p.substring(0, slash);
    if (dir.length() > 0 && !card().exists(dir)) card().mkdir(dir);
    slash = p.indexOf('/', slash + 1);
  }
}

// ---- file I/O -------------------------------------------------------------
bool writeFile(const char* path, const String& data) {
  if (!g_ok) return false;
  ensureParentDirs(path);
  File f = card().open(path, FILE_WRITE);   // FILE_WRITE truncates on the SD lib
  if (!f) { Serial.printf("SD: open-for-write failed: %s\n", path); return false; }
  size_t n = f.print(data);
  f.close();
  return n == data.length();
}

bool appendFile(const char* path, const String& data) {
  if (!g_ok) return false;
  ensureParentDirs(path);
  File f = card().open(path, FILE_APPEND);
  if (!f) { Serial.printf("SD: open-for-append failed: %s\n", path); return false; }
  size_t n = f.print(data);
  f.close();
  return n == data.length();
}

String readFile(const char* path) {
  if (!g_ok) return String();
  File f = card().open(path, FILE_READ);
  if (!f) return String();
  String out;
  out.reserve(f.size());
  while (f.available()) out += (char)f.read();
  f.close();
  return out;
}

bool exists(const char* path) {
  if (!g_ok) return false;
  return card().exists(path);
}

bool remove(const char* path) {
  if (!g_ok) return false;
  return card().remove(path);
}

void listDir(const char* path) {
  if (!g_ok) { Serial.println("SD: not available"); return; }
  File dir = card().open(path);
  if (!dir) { Serial.printf("SD: cannot open dir: %s\n", path); return; }
  if (!dir.isDirectory()) {
    Serial.printf("SD: not a directory: %s\n", path);
    dir.close();
    return;
  }
  Serial.printf("SD: listing %s\n", path);
  for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
    if (e.isDirectory()) Serial.printf("  [DIR]  %s\n", e.name());
    else                 Serial.printf("  %8u  %s\n", (unsigned)e.size(), e.name());
    e.close();
  }
  dir.close();
}

// ---- capacity (MB) --------------------------------------------------------
uint64_t cardSizeMB() { return g_ok ? cardBytes() / MB : 0; }
uint64_t usedMB()     { return g_ok ? usedBytes() / MB : 0; }
uint64_t freeMB() {
  if (!g_ok) return 0;
  const uint64_t total = cardBytes();
  const uint64_t used  = usedBytes();
  return used <= total ? (total - used) / MB : 0;
}

}  // namespace solide::storage
