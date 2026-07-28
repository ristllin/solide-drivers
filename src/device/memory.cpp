#include "solide/memory.h"
#include "solide/storage.h"
#include <Preferences.h>
#include <SD.h>
#include <cstring>

namespace solide::memory {

static Preferences g_prefs;
static bool        g_ok = false;

static bool keyOk(const char* k) { return k && k[0] && strlen(k) <= 15; }

bool begin(const char* nsRoot) {
  // Idempotent. ⚠ Preferences::begin() returns FALSE when the handle is already
  // open, so a second call would clear g_ok and silently break every NVS read
  // and write on the device. A consumer that needs one setting before the full
  // solide::begin() (e.g. which display to bind) must be able to open NVS early
  // without disarming the later call.
  if (g_ok) return true;
  g_ok = g_prefs.begin(nsRoot, /*readOnly=*/false);
  if (!g_ok) Serial.println("memory: NVS begin() failed");
  return g_ok;
}
bool ok() { return g_ok; }

// ---- NVS typed KV -----------------------------------------------------------
String  getString(const char* k, const String& d) { return (g_ok && keyOk(k)) ? g_prefs.getString(k, d) : d; }
int32_t getInt(const char* k, int32_t d)          { return (g_ok && keyOk(k)) ? g_prefs.getInt(k, d) : d; }
bool    getBool(const char* k, bool d)            { return (g_ok && keyOk(k)) ? g_prefs.getBool(k, d) : d; }
float   getFloat(const char* k, float d)          { return (g_ok && keyOk(k)) ? g_prefs.getFloat(k, d) : d; }

bool setString(const char* k, const String& v) { return g_ok && keyOk(k) && (g_prefs.putString(k, v) > 0 || v.isEmpty()); }
bool setInt(const char* k, int32_t v)           { return g_ok && keyOk(k) && g_prefs.putInt(k, v) > 0; }
bool setBool(const char* k, bool v)             { return g_ok && keyOk(k) && g_prefs.putBool(k, v) > 0; }
bool setFloat(const char* k, float v)           { return g_ok && keyOk(k) && g_prefs.putFloat(k, v) > 0; }

bool has(const char* k)      { return g_ok && keyOk(k) && g_prefs.isKey(k); }
bool eraseKey(const char* k) { return g_ok && keyOk(k) && g_prefs.remove(k); }
bool clear()                 { return g_ok && g_prefs.clear(); }

// ---- SD JSON / blob (under /memory/) ----------------------------------------
static String jsonPath(const char* name) { String p = "/memory/"; p += name; p += ".json"; return p; }
static String blobPath(const char* name) { String p = "/memory/blob/"; p += name; p += ".bin"; return p; }

bool putJson(const char* name, const JsonDocument& doc) {
  if (!storage::available()) return false;
  String s;
  serializeJson(doc, s);
  return storage::writeFile(jsonPath(name).c_str(), s);   // creates /memory as needed
}

bool getJson(const char* name, JsonDocument& out) {
  if (!storage::available()) return false;
  String s = storage::readFile(jsonPath(name).c_str());
  if (s.isEmpty()) return false;
  return deserializeJson(out, s) == DeserializationError::Ok;
}

bool putBlob(const char* name, const uint8_t* data, size_t n) {
  if (!storage::available()) return false;
  SD.mkdir("/memory");
  SD.mkdir("/memory/blob");
  File f = SD.open(blobPath(name).c_str(), FILE_WRITE);   // binary-safe (not the String API)
  if (!f) return false;
  size_t w = f.write(data, n);
  f.close();
  return w == n;
}

size_t getBlob(const char* name, uint8_t* out, size_t maxN) {
  if (!storage::available()) return 0;
  File f = SD.open(blobPath(name).c_str(), FILE_READ);
  if (!f) return 0;
  size_t r = f.read(out, maxN);
  f.close();
  return r;
}

bool eraseState(const char* name) {
  if (!storage::available()) return false;
  bool a = SD.remove(jsonPath(name).c_str());
  bool b = SD.remove(blobPath(name).c_str());
  return a || b;
}

}  // namespace solide::memory
