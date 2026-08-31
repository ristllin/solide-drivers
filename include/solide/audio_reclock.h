#pragma once
#include <stdint.h>

// ============================================================================
// Codec re-clock decision (portable, host-tested).
//
// The ES8311 codec drives one shared full-duplex I2S channel; TX (speaker) and
// RX (mic) are the same g_tx/g_rx pair. Changing the sample rate means tearing
// that channel down (i2s_del_channel) and rebuilding it. If a record task is
// parked inside a blocking i2s_channel_read(g_rx) when that teardown runs, the
// RX handle is freed under it: a use-after-free (CUM-272).
//
// This tiny decision is extracted here so the concurrency-sensitive RULE - never
// re-clock while a recording is in flight - is unit-testable on the host, away
// from the I2S/Wire device layer that cannot be compiled natively. The device
// codecInit() calls planReclock() and acts on the returned plan; the host test
// asserts every interleaving of (codecUp, rate change, rxActive).
// ============================================================================

namespace solide::audio {

enum class ReclockPlan {
  NoChange,      // codec already up at this rate: reuse it, no teardown
  Fresh,         // codec not up: build it from scratch (no teardown needed)
  Reclock,       // codec up at a different rate and RX idle: safe to tear down + rebuild
  RefuseRxBusy,  // codec up at a different rate but a recording holds RX: MUST NOT tear down
};

// `requestedRate` is assumed already clamped/normalized to the codec's supported
// range by the caller, so equal rates compare equal.
ReclockPlan planReclock(bool codecUp, uint32_t currentRate, uint32_t requestedRate, bool rxActive);

}  // namespace solide::audio
