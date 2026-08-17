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
