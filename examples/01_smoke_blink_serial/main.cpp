// ============================================================================
// M0 platform de-risk - NO solide drivers.
// Proves the pioarduino toolchain (Arduino-ESP32 3.x / ESP-IDF 5.x) builds,
// USB-CDC serial comes up, a GPIO toggles, and PSRAM/flash report correctly on
// the N16R8. If this doesn't build+run, nothing downstream can - this is the gate.
// ============================================================================
#include <Arduino.h>

#define SMOKE_GPIO 18   // a free spare pin (see board_solide_s3.h reserved list)

static void rule() { Serial.println("--------------------------------------------------"); }

void setup() {
  Serial.begin(115200);
  delay(2000);   // let USB-CDC enumerate before the first print
  rule();
  Serial.println("solide-drivers M0 smoke: platform de-risk");
  Serial.printf("Arduino-ESP32 %d.%d.%d / IDF %s\n",
                ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR,
                ESP_ARDUINO_VERSION_PATCH, esp_get_idf_version());
  Serial.printf("chip %s rev%d, %d cores @ %d MHz\n",
                ESP.getChipModel(), ESP.getChipRevision(),
                ESP.getChipCores(), ESP.getCpuFreqMHz());
  Serial.printf("flash %u MB, PSRAM %.1f MB (expect 16 / 8.0)\n",
                ESP.getFlashChipSize() / (1024u * 1024u),
                ESP.getPsramSize() / (1024.0 * 1024.0));
  rule();
  pinMode(SMOKE_GPIO, OUTPUT);
}

void loop() {
  static uint32_t n = 0;
  static bool on = false;
  on = !on;
  digitalWrite(SMOKE_GPIO, on);
  Serial.printf("[smoke %lus] alive gpio%d=%d PSRAM=%.1fMB heap=%u n=%lu\n",
                millis() / 1000, SMOKE_GPIO, on,
                ESP.getPsramSize() / (1024.0 * 1024.0),
                (unsigned)ESP.getFreeHeap(), (unsigned long)n++);
  delay(1000);
}
