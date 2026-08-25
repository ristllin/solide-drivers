# solide::storage - SD card (raw files)

FAT32 microSD on the FSPI/SPI2 bus (native IOMUX). A tiny stateful wrapper over the
Arduino SD/FS core; every call is a safe no-op when no card is present.

## API
```cpp
bool begin();          // mount (idempotent; safe to retry after inserting a card)
bool available();
bool   writeFile(const char* path, const String& data);   // truncate + write
bool   appendFile(const char* path, const String& data);
String readFile(const char* path);                          // "" on miss
bool   exists(const char* path);   bool remove(const char* path);
void   listDir(const char* path);                           // logs entries to Serial
uint64_t cardSizeMB();  uint64_t usedMB();  uint64_t freeMB();
```
Paths are absolute (`"/logs/boot.txt"`); parent dirs are created as needed.

## Example
`examples/05_storage_sd`.

## Limitations
- No card / mount failure → all calls return `false`/`""`/`0` (never crash); re-call
  `begin()` after inserting a card.
- FAT32 only (not exFAT). Capacity is whole-MB (integer division).
- Distinct from [`solide::memory`](memory.md), which is the typed settings/state store.

## Full-card format
`format()` reformats the ENTIRE mounted card to a fresh FAT volume (FAT or FAT32
by card size; exFAT is not enabled in this FatFs build), then remounts it, on
whichever backend actually mounted (SPI `SD` or on-board `SD_MMC`). It is
destructive: every file on the card
is erased. It refuses cleanly when no card is mounted, returning
`FormatResult::NoCard` with no side effects - it never formats a card it did not
already have open. The return value names the exact stage on failure:

| `FormatResult` | Meaning |
|---|---|
| `Ok` | Card reformatted and remounted, ready for I/O. |
| `NoCard` | No card mounted; refused before touching anything. |
| `MkfsFailed` | `f_mkfs` returned non-OK (bad or locked card, too small). |
| `RemountFailed` | Format succeeded but the card would not re-mount after. |

The refuse-when-absent guard and the mkfs-then-remount ordering live in the
portable, Arduino-free `solide/storage_format.h` so they are host-tested
(`test/test_storage_format/`). The on-card destructive behavior (that a real card
comes back blank and mountable) is a hardware test in the consuming firmware's HIL
suite, gated on a scratch card.

## Example
`examples/05_storage_sd`.

## Limitations
- No card / mount failure → all calls return `false`/`""`/`0` (never crash); re-call
  `begin()` after inserting a card.
- FAT32 only (not exFAT). Capacity is whole-MB (integer division).
- Distinct from [`solide::memory`](memory.md), which is the typed settings/state store.
