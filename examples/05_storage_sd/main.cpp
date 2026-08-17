// solide-drivers example: SD storage - mount, write, read back, list, capacity.
// 3.3 V. Needs a FAT32 microSD inserted (gracefully reports if absent).
#include <Arduino.h>
#include <solide/storage.h>

using namespace solide;

void setup() {
  Serial.begin(115200);
  delay(1000);
  if (!storage::begin()) {
    Serial.println("SD: no card / mount failed (insert a FAT32 microSD)");
    return;
  }
  Serial.printf("SD mounted: %llu MB total, %llu MB free\n",
                storage::cardSizeMB(), storage::freeMB());
  bool w = storage::writeFile("/demo/hello.txt", "hi from solide-drivers");
  String back = storage::readFile("/demo/hello.txt");
  Serial.printf("write=%d read-back=\"%s\"\n", w, back.c_str());
  storage::appendFile("/demo/log.txt", "boot\n");
  storage::listDir("/demo");
}

void loop() { delay(1000); }
