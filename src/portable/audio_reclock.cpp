#include "solide/audio_reclock.h"

namespace solide::audio {

ReclockPlan planReclock(bool codecUp, uint32_t currentRate, uint32_t requestedRate, bool rxActive) {
  if (!codecUp) return ReclockPlan::Fresh;              // nothing up yet: build fresh
  if (requestedRate == currentRate) return ReclockPlan::NoChange;  // same rate: cheap no-op
  if (rxActive) return ReclockPlan::RefuseRxBusy;       // rate change would free g_rx under a live read
  return ReclockPlan::Reclock;                          // rate change, RX idle: safe teardown + rebuild
}

}  // namespace solide::audio
