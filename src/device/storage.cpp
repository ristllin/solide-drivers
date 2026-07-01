#include "solide/storage.h"
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include "solide/boards/board_solide_s3.h"

// SD bus pins from the canonical board config (FSPI/SPI2 native IOMUX).
static constexpr int SD_CS   = solide::kBoardSolideS3.sd.cs;
static constexpr int SD_MOSI = solide::kBoardSolideS3.sd.mosi;
static constexpr int SD_SCK  = solide::kBoardSolideS3.sd.sck;
static constexpr int SD_MISO = solide::kBoardSolideS3.sd.miso;

// microSD over the dedicated FSPI/SPI2 bus (native IOMUX pins → fast). The init
// sequence is exactly the one validated in the bring-up validator (main.cpp):
//   SPIClass(FSPI).begin(SCK, MISO, MOSI, CS)  then  SD.begin(CS, spi)
// We keep one module-static SPIClass + availability flag, sysfs-style.

namespace solide::storage {

static SPIClass g_spi(FSPI);
static bool     g_ok = false;

static const uint64_t MB = 1024ULL * 1024ULL;

// ---- mount ----------------------------------------------------------------
bool begin() {
  if (g_ok) return true;   // already mounted — idempotent

  g_spi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, g_spi)) {
    Serial.println("SD: begin() failed — check CS=10 MOSI=11 CLK=12 MISO=13, "
                   "3V3 power, card inserted/FAT32. (running without SD)");
    g_ok = false;
    return false;
  }

  uint8_t ct = SD.cardType();
  if (ct == CARD_NONE) {
    Serial.println("SD: no card detected (running without SD)");
    SD.end();
    g_ok = false;
    return false;
  }
  const char* tn = ct == CARD_MMC  ? "MMC"
                 : ct == CARD_SD   ? "SDSC"
                 : ct == CARD_SDHC ? "SDHC"
                 : "UNKNOWN";
  Serial.printf("SD: %s, %llu MB total, %llu MB used\n",
                tn, SD.cardSize() / MB, SD.usedBytes() / MB);

  g_ok = true;
  return true;
}

bool available() { return g_ok; }

// ---- helpers --------------------------------------------------------------
// Create every parent dir of `path` (mkdir is a no-op if it already exists).
// "/logs/sub/boot.txt" → mkdir("/logs"), mkdir("/logs/sub").
static void ensureParentDirs(const char* path) {
  if (!path || path[0] != '/') return;
  String p(path);
  int slash = p.indexOf('/', 1);
  while (slash > 0) {
    String dir = p.substring(0, slash);
    if (dir.length() > 0 && !SD.exists(dir)) SD.mkdir(dir);
    slash = p.indexOf('/', slash + 1);
  }
}

// ---- file I/O -------------------------------------------------------------
bool writeFile(const char* path, const String& data) {
  if (!g_ok) return false;
  ensureParentDirs(path);
  File f = SD.open(path, FILE_WRITE);   // FILE_WRITE truncates on the SD lib
  if (!f) { Serial.printf("SD: open-for-write failed: %s\n", path); return false; }
  size_t n = f.print(data);
  f.close();
  return n == data.length();
}

bool appendFile(const char* path, const String& data) {
  if (!g_ok) return false;
  ensureParentDirs(path);
  File f = SD.open(path, FILE_APPEND);
  if (!f) { Serial.printf("SD: open-for-append failed: %s\n", path); return false; }
  size_t n = f.print(data);
  f.close();
  return n == data.length();
}

String readFile(const char* path) {
  if (!g_ok) return String();
  File f = SD.open(path, FILE_READ);
  if (!f) return String();
  String out;
  out.reserve(f.size());
  while (f.available()) out += (char)f.read();
  f.close();
  return out;
}

bool exists(const char* path) {
  if (!g_ok) return false;
  return SD.exists(path);
}

bool remove(const char* path) {
  if (!g_ok) return false;
  return SD.remove(path);
}

void listDir(const char* path) {
  if (!g_ok) { Serial.println("SD: not available"); return; }
  File dir = SD.open(path);
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
uint64_t cardSizeMB() { return g_ok ? SD.cardSize() / MB : 0; }
uint64_t usedMB()     { return g_ok ? SD.usedBytes() / MB : 0; }
uint64_t freeMB() {
  if (!g_ok) return 0;
  uint64_t total = SD.cardSize();
  uint64_t used  = SD.usedBytes();
  return used <= total ? (total - used) / MB : 0;
}

}  // namespace solide::storage
