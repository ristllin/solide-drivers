# solide::memory - persistent settings/state

Two backends by size: small typed config on **NVS** (Arduino `Preferences`, survives
reflash), larger JSON/blobs on **SD** under `/memory/`. Distinct from
[`solide::storage`](storage.md) (raw files).

## API
```cpp
bool begin(const char* nsRoot = "solide");   bool ok();
// NVS typed KV (keys <= 15 chars):
String  getString(const char* k, const String& def = "");   bool setString(const char* k, const String& v);
int32_t getInt(const char* k, int32_t def = 0);              bool setInt(const char* k, int32_t v);
bool    getBool(const char* k, bool def = false);            bool setBool(const char* k, bool v);
float   getFloat(const char* k, float def = 0);              bool setFloat(const char* k, float v);
bool has(const char* k);   bool eraseKey(const char* k);   bool clear();
// SD JSON / blob (under /memory/; needs a mounted card):
bool   putJson(const char* name, const JsonDocument& doc);   bool getJson(const char* name, JsonDocument& out);
bool   putBlob(const char* name, const uint8_t* data, size_t n);   size_t getBlob(const char* name, uint8_t* out, size_t maxN);
bool   eraseState(const char* name);
```

## Example
`examples/06_memory_kv` (persistent boot counter + JSON state).

## Limitations
- **NVS keys must be ≤ 15 chars** (hardware limit) - longer keys are rejected.
- The JSON/blob half needs a mounted SD card (call `storage::begin()` first); without a
  card it returns `false` while the NVS half keeps working (boot-critical config survives).
- `clear()` wipes the whole NVS namespace (a settings factory-reset).
