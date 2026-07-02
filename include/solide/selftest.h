#pragma once

// ============================================================================
// solide::selftest — per-peripheral self-tests + an agent-drivable serial
// protocol. Call poll() every loop(). A host/agent sends a line:
//   TEST <led|epd|sd|memory|input|audio|spk|mic|all>
// (audio = speaker->mic acoustic loopback; spk = speaker-only audible tone;
//  mic = mic-only RMS monitor that responds to tapping — use spk+mic to bisect
//  an audio SKIP into a speaker/coupling fault vs a mic fault.)
// and the device replies:
//   RESULT <name> PASS|FAIL|SKIP <k=v> ...
// (SKIP = optional hardware absent, e.g. no SD card, not a failure.)
// "INFO" replies with a board/heap/psram/uptime line.
// ============================================================================
namespace solide::selftest {

void poll();                  // drain Serial; dispatch TEST/INFO lines. Call each loop().
bool run(const char* name);   // run one check now, print its RESULT line, return pass

}  // namespace solide::selftest
