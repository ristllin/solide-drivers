#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// ============================================================================
// solide::memory - persistent settings/state store. Two backends by size:
//   * small typed config -> NVS (Arduino Preferences): survives reflash, atomic.
//   * larger JSON / blobs -> SD under /memory/ (needs a mounted card).
// Distinct from solide::storage (raw SD files). Graceful: the NVS half works with
// no SD card; the SD half returns false/0 when the card is absent.
// ============================================================================
namespace solide::memory {

bool begin(const char* nsRoot = "solide");   // open the NVS namespace
bool ok();                                    // NVS namespace open?

// ---- typed key-value (NVS) - keys must be <= 15 chars (NVS limit) -----------
String  getString(const char* key, const String& def = "");
int32_t getInt(const char* key, int32_t def = 0);
bool    getBool(const char* key, bool def = false);
float   getFloat(const char* key, float def = 0.0f);
bool setString(const char* key, const String& v);
bool setInt(const char* key, int32_t v);
bool setBool(const char* key, bool v);
bool setFloat(const char* key, float v);
bool has(const char* key);
bool eraseKey(const char* key);
bool clear();                                 // wipe the whole namespace (settings reset)

// ---- larger JSON / blob state on SD (under /memory/) ------------------------
bool   putJson(const char* name, const JsonDocument& doc);   // /memory/<name>.json
bool   getJson(const char* name, JsonDocument& out);         // false if absent/parse-fail
bool   putBlob(const char* name, const uint8_t* data, size_t n);  // /memory/blob/<name>.bin
size_t getBlob(const char* name, uint8_t* out, size_t maxN);      // bytes read
bool   eraseState(const char* name);

}  // namespace solide::memory
