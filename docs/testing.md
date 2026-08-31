# Testing

Three layers - two you can run anywhere, one that needs the board.

## 1. Native host tests (no hardware)

```bash
pio test -e native
```
Unit-tests the portable core (`solide::ring` / `input` / `menu` / `wav` / `tone`) on
the host via Unity - allocator/layout/animation math, quadrature + debounce, the
menu FSM, WAV parse/build, Goertzel tone detection. Runs in CI on every push
(`.github/workflows/native-tests.yml`).

It also pins the TFT panel-health compare (`solide/panel_health.h`, which
`display_tft.cpp::healthy()` delegates to) in `test_panel_health`: a table-driven
counter-test over every bit of the `0xFE` RDDST compare mask, and an anti-thrash
test that a legitimate flip toggle drives zero heals and zero health-state
transitions.

**Standing rule (do not skip).** Loosening a health-compare mask, a timeout, or a
validation bound MUST ship a counter-test proving the newly-ignored dimension is
still detected. The panel-health mask earned this the hard way: it was once
loosened `0xFE -> 0x3E`, silently dropping the MY/MX fault detection and bringing
back the white screen, while the suite stayed green because nothing asserted a
fault in the dropped bits. The `test_panel_health` counter-test now fails loudly
on exactly that change.

## 2. On-device self-test protocol (serial)

Flash `examples/08_selftest_console`, then send line commands over serial:

```
TEST all      -> RESULT led PASS ... / RESULT epd PASS ... / ... / RESULT all PASS (n/m)
TEST <name>   -> RESULT <name> PASS|FAIL|SKIP <k=v>...   (name: led epd sd memory input audio)
INFO          -> INFO board=... psramMB=... heap=... uptime=...
```
`SKIP` = an optional/unpowered peripheral (e.g. no SD card, or audio without the 5 V
amp) - not a failure. It's line-oriented so a human or an agent can drive it.

## 3. Device-in-the-loop tests (pytest)

```bash
pip install -r tools/requirements.txt
pytest test_device -m device        # skips automatically if no board is attached
```
`tools/device_harness.py` opens the serial port, reads in a background thread, drives
the `TEST` protocol, and fails on any crash marker (`Guru Meditation`, `rst:0x`,
`Backtrace:`, …). This suite already caught a real firmware crash (a concurrent
`i2s_new_channel` race in the audio loopback).

`tools/solide_console.py` is a standalone auto-reconnecting serial recorder
(`python tools/solide_console.py [--test all]`) for soak runs / bring-up.

## Validate, don't eyeball
The evidence is the `RESULT ... PASS` lines, the measured refresh timings, and a flat
heap across frames - not a glance at the board. 5 V-dependent checks (LED lighting,
speaker audibility, the acoustic loopback) need the 5 V bus powered.
