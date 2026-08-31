#pragma once
#include <cstdint>

// ============================================================================
// Portable liveness + recovery policy for the capacitive touch path (FT6336U
// over a SHARED I2C bus). No Arduino dependencies - the hardware glue (Wire,
// bus-clear SCL pulses, TC_RST) lives in src/device/touch.cpp and feeds this
// state machine the outcome of each attempt, then runs whatever step it asks
// for. This split keeps the ladder host-testable (pio test -e native).
//
// Why this exists (CUM-248): the old ftReadPoint() returned false on ANY I2C
// failure with no recovery, no re-probe, no visibility. If the controller
// dropped off the bus after long idle (its default is to auto-enter a low-power
// Monitor mode) or the shared SDA wedged once, every later read failed forever,
// silently, until a power-cycle re-ran begin()'s reset. This class detects the
// dead-comms streak and drives a bus-clear -> hard-reset -> backoff ladder, and
// exposes counters so firmware can see it and tests can assert it.
// ============================================================================

namespace solide::touch {

// Health snapshot for the capacitive controller. Firmware logs/telemeters this;
// tests assert on it. Counters are monotonic except consecutiveFailures (the
// live streak, 0 when healthy). On a resistive board these stay all-zero.
struct Health {
  uint32_t failures = 0;             // total I2C transaction failures observed
  uint32_t recoveries = 0;           // times comms was regained after degrading
  uint32_t busClears = 0;            // bus-clear pulse sequences issued
  uint32_t hardResets = 0;           // TC_RST re-resets issued
  uint16_t consecutiveFailures = 0;  // current failure streak (0 when healthy)
  uint32_t lastRecoveryMs = 0;       // millis() of the most recent recovery
  bool     degraded = false;         // true while comms is not confirmed alive
};

// Recovery policy. Fed one transaction outcome per poll cycle; returns the next
// step the device layer should run. All hardware stays in the device layer.
//
// Ladder after K consecutive transaction failures: bus-clear -> hard reset ->
// exponential backoff, re-probing on each rung. Any successful transaction
// resets the streak and, if we had degraded, counts one recovery.
class Liveness {
 public:
  enum class Step : uint8_t {
    Attempt,    // do a normal read this cycle
    BusClear,   // pulse SCL to free a wedged SDA, then ACK-probe
    HardReset,  // toggle TC_RST + re-probe (the begin() dance)
    Wait,       // in backoff: skip this cycle entirely
  };

  struct Config {
    // K: consecutive faults before the ladder engages. 4 was chosen against the
    // 20 ms (50 Hz) poll cadence: ~80 ms of confirmed-dead comms, long enough to
    // ignore a lone glitched transaction, short enough to recover well inside a
    // human's "why is this dead" window.
    uint8_t  failThreshold = 4;
    // Backoff after a full ladder miss: base, doubling to the ceiling, so a
    // truly-absent controller is probed every few seconds instead of hammered at
    // 50 Hz (which would also starve the shared codec).
    uint32_t backoffBaseMs = 100;
    uint32_t backoffMaxMs  = 4000;
  };

  Liveness() = default;
  explicit Liveness(const Config& cfg) : cfg_(cfg) {}

  // What should the device do this poll cycle? Pure query (no state change).
  Step plan(uint32_t nowMs) const;

  // Report the outcome of the step plan() returned. `ok` means comms is alive:
  // for Attempt, the I2C read transaction completed (finger present or not);
  // for BusClear / HardReset, the follow-up ACK probe succeeded.
  void report(Step step, bool ok, uint32_t nowMs);

  Health health() const;
  const Config& config() const { return cfg_; }

 private:
  enum class St : uint8_t { Normal, Recover, Backoff };
  void recovered(uint32_t nowMs);
  void enterBackoff(uint32_t nowMs);

  Config   cfg_{};
  St       st_ = St::Normal;
  uint8_t  rung_ = 0;               // within Recover: 0 = bus-clear, 1 = hard-reset
  uint16_t consecFail_ = 0;
  uint32_t backoffMs_ = 0;          // current backoff width (0 before the first)
  uint32_t backoffUntil_ = 0;       // millis() the current backoff expires
  uint32_t failures_ = 0;
  uint32_t recoveries_ = 0;
  uint32_t busClears_ = 0;
  uint32_t hardResets_ = 0;
  uint32_t lastRecoveryMs_ = 0;
};

}  // namespace solide::touch
