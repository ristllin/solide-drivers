"""Device-in-the-loop tests driving the serial TEST protocol.

Run with the board flashed with examples/08_selftest_console and attached over USB:
    pytest test_device -m device
Skips automatically if no device is present.
"""
import re

import pytest


@pytest.mark.device
def test_all_peripherals_pass(harness):
    t0 = harness.now()
    harness.send("TEST all")
    line = harness.wait_for("RESULT all", timeout=25, since=t0)
    assert line is not None, "no 'RESULT all' reply within 25 s"
    m = re.search(r"RESULT all (PASS|FAIL) \((\d+)/(\d+)\)", line)
    assert m, f"malformed summary line: {line}"
    assert m.group(1) == "PASS", f"suite not all-pass: {line}"
    harness.assert_no_crash(since=t0)


@pytest.mark.device
@pytest.mark.parametrize("name", ["led", "epd", "memory", "input"])
def test_core_peripheral(harness, name):
    """The always-present peripherals must PASS (SD/audio may SKIP if absent)."""
    t0 = harness.now()
    harness.send(f"TEST {name}")
    line = harness.wait_for(f"RESULT {name} ", timeout=10, since=t0)
    assert line is not None, f"no 'RESULT {name}' reply"
    assert " PASS" in line, f"{name} did not PASS: {line}"
    harness.assert_no_crash(since=t0)


@pytest.mark.device
@pytest.mark.parametrize("name", ["sd", "audio"])
def test_optional_peripheral(harness, name):
    """Optional/5 V-dependent peripherals must at least respond PASS or SKIP (never crash)."""
    t0 = harness.now()
    harness.send(f"TEST {name}")
    line = harness.wait_for(f"RESULT {name} ", timeout=15, since=t0)
    assert line is not None, f"no 'RESULT {name}' reply"
    assert (" PASS" in line) or (" SKIP" in line), f"{name} neither PASS nor SKIP: {line}"
    harness.assert_no_crash(since=t0)


@pytest.mark.device
def test_info(harness):
    t0 = harness.now()
    harness.send("INFO")
    line = harness.wait_for("INFO board=", timeout=6, since=t0)
    assert line is not None, "no INFO reply"
    assert "psramMB=8" in line, f"expected 8 MB PSRAM: {line}"
