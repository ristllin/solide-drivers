#include "solide/touch.h"

#include <SPI.h>

#include "solide/board.h"
#include "solide/boards/board_solide_s3.h"
#include "solide/display_tft.h"

// ============================================================================
// XPT2046 resistive touch controller.
//
// Shares the display's SPI bus (its T_CLK/T_DIN/T_DO are bridged onto the
// panel's SCK/SDI/SDO on the module) with its own chip select. It is a SLOW
// device — the datasheet tops out around 2 MHz against the panel's 40 — so
// every read opens its own transaction at its own speed. Getting this wrong
// does not fail loudly; it returns plausible-looking garbage coordinates.
// ============================================================================

namespace {

constexpr int8_t T_CS = solide::kBoardSolideS3.tft.tcs;

// 2 MHz: inside the controller's limit with margin. A conversion is 3 bytes.
const SPISettings kTouchSPI(2000000, MSBFIRST, SPI_MODE0);

// Control bytes: START(7) | A2..A0(6:4) | MODE(3)=0 12-bit | SER/DFR(2)=0 diff.
// ⚠ The channel is bits 6:4, so read them from the byte, not from intuition:
//   0xD0 = 1101_0000 -> A=101 = X position
//   0x90 = 1001_0000 -> A=001 = Y position
// These were transposed at first, which does NOT look broken — both axes stay
// in range, so every tap simply lands somewhere else on the panel.
constexpr uint8_t CMD_X = 0xD0;   // A2..A0 = 101 -> X position
constexpr uint8_t CMD_Y = 0x90;   // A2..A0 = 001 -> Y position
constexpr uint8_t CMD_Z1 = 0xB0;  // A2..A0 = 011
constexpr uint8_t CMD_Z2 = 0xC0;  // A2..A0 = 100

// Below this raw pressure the panel is considered untouched. Resistive panels
// report a rising Z as contact firms up; this threshold rejects the noise floor
// without demanding a hard press.
constexpr uint16_t kZThreshold = 350;

// Debounce: a touch must be seen on consecutive samples to count as down, and
// missing must be seen consecutively to count as up. Resistive panels chatter
// badly at the moment of contact and release.
constexpr uint8_t kDebounce = 2;

// Reads are rate-limited: polling SPI every loop iteration would starve the
// bus the display is trying to use.
constexpr uint32_t kPollIntervalMs = 20;   // 50 Hz — well above finger speed

bool                    g_present = false;
solide::touch::Calibration g_cal;
// ⚠ g_cal is written from the WEB task (a calibration save) and read from the
// main loop, on a dual-core S3 — genuinely in parallel, not merely preempted.
// A 7-field struct assignment is not atomic, so an unguarded read can observe a
// torn mix of old and new values and place a tap somewhere neither calibration
// would. The lock is held only for the copy (a few dozen cycles), never across
// SPI, so it cannot delay a transfer.
portMUX_TYPE g_calMux = portMUX_INITIALIZER_UNLOCKED;

solide::touch::Calibration calSnapshot() {
  portENTER_CRITICAL(&g_calMux);
  const solide::touch::Calibration c = g_cal;
  portEXIT_CRITICAL(&g_calMux);
  return c;
}
solide::touch::Point    g_state;
uint8_t                 g_downCount = 0, g_upCount = 0;
uint32_t                g_lastPollMs = 0;

// The panel owns the bus; we borrow it (the module bridges T_CLK/T_DIN/T_DO
// onto its SCK/SDI/SDO). display_tft::begin() must have run first — touch::begin()
// is ordered after it at the composition root.
inline SPIClass& bus() { return *solide::display_tft::bus(); }

uint16_t readChannel(uint8_t cmd) {
  // 12-bit result arrives MSB-first across the two bytes following the command,
  // shifted left by 3.
  bus().transfer(cmd);
  const uint8_t hi = bus().transfer(0x00);
  const uint8_t lo = bus().transfer(0x00);
  return uint16_t(((hi << 8) | lo) >> 3) & 0x0FFF;
}

// One full sample inside a single transaction.
bool sampleRaw(uint16_t& x, uint16_t& y, uint16_t& z) {
  bus().beginTransaction(kTouchSPI);
  digitalWrite(T_CS, LOW);

  const uint16_t z1 = readChannel(CMD_Z1);
  const uint16_t z2 = readChannel(CMD_Z2);
  // Pressure rises as z1 grows and z2 falls; this difference is the standard
  // cheap estimate and is all we need for a threshold.
  const int32_t pressure = int32_t(z1) - int32_t(z2) + 4095;

  uint16_t xs = 0, ys = 0;
  if (pressure > kZThreshold) {
    // Two conversions per axis, taking the second: the first after switching
    // channels is taken while the input is still settling.
    readChannel(CMD_X); xs = readChannel(CMD_X);
    readChannel(CMD_Y); ys = readChannel(CMD_Y);
  }

  digitalWrite(T_CS, HIGH);
  bus().endTransaction();

  x = xs; y = ys;
  z = uint16_t(pressure < 0 ? 0 : (pressure > 65535 ? 65535 : pressure));
  return pressure > kZThreshold && xs > 0 && ys > 0;
}

int16_t mapClamped(uint16_t v, uint16_t lo, uint16_t hi, int16_t outMax, bool invert) {
  if (hi <= lo) return 0;
  if (v < lo) v = lo;
  if (v > hi) v = hi;
  int32_t p = (int32_t(v - lo) * outMax) / int32_t(hi - lo);
  if (invert) p = outMax - p;
  if (p < 0) p = 0;
  if (p > outMax) p = outMax;
  return int16_t(p);
}

}  // namespace

namespace solide::touch {

bool begin() {
  if (T_CS < 0) return false;   // no touch panel on this board
  pinMode(T_CS, OUTPUT);
  digitalWrite(T_CS, HIGH);
  // One throwaway conversion powers the controller's reference up; the very
  // first read after cold boot is otherwise unreliable.
  uint16_t x, y, z;
  sampleRaw(x, y, z);
  g_present = true;
  return true;
}

bool present() { return g_present; }

bool readRaw(uint16_t& x, uint16_t& y, uint16_t& z) {
  if (!g_present) return false;
  return sampleRaw(x, y, z);
}

Point read() {
  if (!g_present) return Point{};

  const uint32_t now = millis();
  if (now - g_lastPollMs < kPollIntervalMs) return g_state;   // cached between polls
  g_lastPollMs = now;

  uint16_t rx = 0, ry = 0, rz = 0;
  const bool hit = sampleRaw(rx, ry, rz);

  if (hit) {
    g_upCount = 0;
    if (g_downCount < kDebounce) g_downCount++;
  } else {
    g_downCount = 0;
    if (g_upCount < kDebounce) g_upCount++;
  }

  if (g_downCount >= kDebounce) {
    // One consistent snapshot for the whole mapping — mixing fields from two
    // calibrations would land the tap somewhere neither of them describes.
    const Calibration cal = calSnapshot();
    uint16_t ax = rx, ay = ry;
    if (cal.swapXY) { const uint16_t t = ax; ax = ay; ay = t; }
    g_state.x = mapClamped(ax, cal.minX, cal.maxX,
                           solide::display_tft::kW - 1, cal.invertX);
    g_state.y = mapClamped(ay, cal.minY, cal.maxY,
                           solide::display_tft::kH - 1, cal.invertY);
    g_state.pressure = rz;
    g_state.down = true;
  } else if (g_upCount >= kDebounce) {
    g_state = Point{};   // x/y back to -1: coordinates are meaningless when up
  }
  return g_state;
}

void setCalibration(const Calibration& c) {
  portENTER_CRITICAL(&g_calMux);
  g_cal = c;
  portEXIT_CRITICAL(&g_calMux);
}
Calibration calibration() { return calSnapshot(); }

}  // namespace solide::touch
