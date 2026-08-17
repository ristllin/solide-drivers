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
