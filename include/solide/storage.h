#pragma once
#include <Arduino.h>
#include <FS.h>

// ============================================================================
// Nuage Solide S3 - microSD storage helper
//
// Two card backends selected by board data (sdKind), never by the caller: a
// generic SPI microSD module (FAT32) on the dedicated FSPI/SPI2 bus (Solide S3,
// native IOMUX pins board.h: SD_CS=10 / SD_MOSI=11 / SD_SCK=12 / SD_MISO=13),
// or the on-board SDMMC/SDIO 4-bit slot (Freenove). Both expose the same fs::FS
// surface downstream - see activeFs() below for callers that need to route
// their OWN file I/O through the mounted card (e.g. the orchestrator's memory
// backend), rather than going through this module's own writeFile/readFile.
//
// Style mirrors sys/fs.cpp (sysfs::): a tiny stateful module guarded by an
// "available" flag, robust to a missing card or a failed mount - every call is
// a no-op that returns false / "" / 0 when the card is absent, never a crash.
//
// Uses the Arduino-ESP32 built-in SD.h / SD_MMC.h + FS.h - no extra lib_deps.
// ============================================================================
namespace solide::storage {

// Mount the card: bind a SPIClass to the FSPI bus on the board.h pins, run
// SD.begin(SD_CS, spi), and log the detected card type + size. Returns false
// (and leaves the module unavailable) on no-card / begin failure. Safe to call
// again to retry after inserting a card.
bool begin();

// True once begin() has mounted a card. All other calls require this.
bool available();

// The fs::FS begin() actually mounted (SD or SD_MMC, chosen by board sdKind) -
// valid once available() is true. Callers that need to route their OWN file I/O
// through the card (rather than this module's writeFile/readFile helpers) must
// go through this, not assume the SPI `SD` global: on an SDMMC board (Freenove)
// the card is mounted via SD_MMC and `SD` is never begun (its pins are often
// repurposed elsewhere), so hardcoding `SD` silently talks to an unmounted
// filesystem there.
fs::FS& activeFs();

// The card type of whichever backend is active (CARD_NONE/CARD_MMC/CARD_SD/
// CARD_SDHC, from SD.h) - for diagnostics, valid to call even when available()
// is false (e.g. to report why begin() failed).
uint8_t cardType();

// Unmount + release the SPI bus, clearing the internal "mounted" latch so the
// NEXT begin() does a real re-probe (SD.end() + SD.begin()) rather than the
// idempotent no-op. Needed to recover a card that was pulled and re-seated
// mid-run: end() then begin() is the only way to re-detect it without a reboot.
// A no-op if nothing is mounted. Safe to call any time.
void end();

// ---- File I/O -------------------------------------------------------------
// Paths are absolute ("/logs/boot.txt"); parent dirs are created as needed.
bool   writeFile(const char* path, const String& data);   // truncate + write
bool   appendFile(const char* path, const String& data);  // append (create if new)
String readFile(const char* path);                         // whole file, "" on miss
bool   exists(const char* path);
bool   remove(const char* path);

// Log every direct entry under `path` (name + size for files, [DIR] for dirs).
void   listDir(const char* path);

// ---- Capacity (MB) --------------------------------------------------------
uint64_t cardSizeMB();   // total card size
uint64_t usedMB();       // used bytes  (SD.usedBytes())
uint64_t freeMB();       // cardSizeMB() - usedMB()

}  // namespace solide::storage
