#include "solide/input.h"
#include <Arduino.h>
#include "solide/encoder_decode.h"   // QuadDecoder / Button (portable core)
#include "esp_heap_caps.h"
#include "solide/boards/board_solide_s3.h"

// EC11 pins from the canonical board config.
static constexpr int ENC_A  = solide::kBoardSolideS3.enc.a;
static constexpr int ENC_B  = solide::kBoardSolideS3.enc.b;
static constexpr int ENC_SW = solide::kBoardSolideS3.enc.sw;

namespace solide::input {

static QueueHandle_t     q   = nullptr;
static QuadDecoder dec(4);                       // 4 quarter-steps per detent
static Button      btn(50, BTN_LONGPRESS_MS);    // 50 ms debounce
static volatile bool     g_sw = false;                 // debounced switch state
static volatile bool     g_taskAlive = false;          // self-test liveness flag

static void push(Event e) { if (q) xQueueSend(q, &e, 0); }   // drop if full

static void task(void*) {
  g_taskAlive = true;
  for (;;) {
    int8_t d = dec.update(digitalRead(ENC_A), digitalRead(ENC_B));
    if (d > 0)      push(Event::RotateCW);
    else if (d < 0) push(Event::RotateCCW);

    bool sw = digitalRead(ENC_SW) == LOW;
    BtnEvent be = btn.update(sw, millis());
    g_sw = btn.isPressed();   // debounced -> a contact bounce can't abort hold-to-talk
    if (be == BtnEvent::Click)          push(Event::Click);
    else if (be == BtnEvent::LongPress) push(Event::LongPress);

    vTaskDelay(pdMS_TO_TICKS(1));   // 1 kHz poll
  }
}

bool begin() {
  if (q) return true;        // idempotent — a second call is a safe no-op
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);
  q = xQueueCreate(16, sizeof(Event));
  if (!q) { Serial.printf("enc: queue create FAILED heap=%u\n", ESP.getFreeHeap()); return false; }
  BaseType_t ok = xTaskCreatePinnedToCore(task, "enc", 2560, nullptr, 2, nullptr, 1);
  if (ok != pdPASS) { Serial.printf("enc: task create FAILED heap=%u\n", ESP.getFreeHeap()); return false; }
  return true;
}

bool pop(Event& e) { return q && xQueueReceive(q, &e, 0) == pdTRUE; }
bool pressed()     { return g_sw; }
bool taskAlive()   { return g_taskAlive; }

}  // namespace solide::input
