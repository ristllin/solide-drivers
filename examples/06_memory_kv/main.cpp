// solide-drivers example: persistent memory - NVS typed key-value (survives
// reflash) + a JSON blob on SD. 3.3 V. The NVS half works without an SD card.
#include <Arduino.h>
#include <solide/memory.h>
#include <solide/storage.h>

using namespace solide;

void setup() {
  Serial.begin(115200);
  delay(1000);
  storage::begin();               // enables the SD-backed JSON half (optional)
  memory::begin("demo");

  int boots = memory::getInt("boots", 0) + 1;   // survives power-cycles + reflash
  memory::setInt("boots", boots);
  memory::setString("name", "solide");
  Serial.printf("boot #%d, name=%s\n", boots, memory::getString("name", "?").c_str());

  JsonDocument doc;
  doc["fw"] = "0.1.0-dev";
  doc["boots"] = boots;
  if (memory::putJson("state", doc)) Serial.println("wrote /memory/state.json");
  else Serial.println("(no SD - JSON state skipped; NVS still persisted)");
}

void loop() { delay(1000); }
