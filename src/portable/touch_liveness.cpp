#include "solide/touch_liveness.h"

namespace solide::touch {

Liveness::Step Liveness::plan(uint32_t nowMs) const {
  switch (st_) {
    case St::Normal:
      return Step::Attempt;
    case St::Recover:
      return rung_ == 0 ? Step::BusClear : Step::HardReset;
    case St::Backoff:
      // Signed diff so millis() wraparound does not strand us in Wait forever.
      if (int32_t(nowMs - backoffUntil_) < 0) return Step::Wait;
      return Step::Attempt;   // backoff elapsed: one plain retry before re-laddering
  }
  return Step::Attempt;
}

void Liveness::recovered(uint32_t nowMs) {
  recoveries_++;
  lastRecoveryMs_ = nowMs;
  st_ = St::Normal;
  rung_ = 0;
  consecFail_ = 0;
  backoffMs_ = 0;
}

void Liveness::enterBackoff(uint32_t nowMs) {
  // Exponential: base, then double each full-ladder miss, capped at the ceiling.
  if (backoffMs_ == 0) {
    backoffMs_ = cfg_.backoffBaseMs;
  } else if (backoffMs_ < cfg_.backoffMaxMs) {
    backoffMs_ *= 2;
    if (backoffMs_ > cfg_.backoffMaxMs) backoffMs_ = cfg_.backoffMaxMs;
  }
  backoffUntil_ = nowMs + backoffMs_;
  st_ = St::Backoff;
  rung_ = 0;
}

void Liveness::report(Step step, bool ok, uint32_t nowMs) {
  switch (step) {
    case Step::Attempt:
      if (ok) {
        if (st_ != St::Normal) recovered(nowMs);   // regained comms after degrading
        else consecFail_ = 0;                       // healthy read: clear any glitch streak
      } else {
        failures_++;
        if (consecFail_ != 0xFFFF) consecFail_++;
        if (st_ == St::Backoff) {
          st_ = St::Recover;   // post-backoff probe still dead: run the ladder again
          rung_ = 0;
        } else if (st_ == St::Normal && consecFail_ >= cfg_.failThreshold) {
          st_ = St::Recover;   // streak crossed K: engage the ladder
          rung_ = 0;
        }
      }
      break;
    case Step::BusClear:
      busClears_++;
      if (ok) recovered(nowMs);
      else rung_ = 1;          // still dead: escalate to a hard reset next cycle
      break;
    case Step::HardReset:
      hardResets_++;
      if (ok) recovered(nowMs);
      else enterBackoff(nowMs); // full ladder missed: back off, then retry later
      break;
    case Step::Wait:
      break;                    // backoff idle: nothing happened
  }
}

Health Liveness::health() const {
  Health h;
  h.failures = failures_;
  h.recoveries = recoveries_;
  h.busClears = busClears_;
  h.hardResets = hardResets_;
  h.consecutiveFailures = consecFail_;
  h.lastRecoveryMs = lastRecoveryMs_;
  h.degraded = (st_ != St::Normal);
  return h;
}

}  // namespace solide::touch
