"""Serial-only device harness for solide-drivers.

Drives the on-device self-test protocol over USB-CDC and asserts on the RESULT
lines. Trimmed from the classic Nuage-Solide DeviceHarness — all HTTP/web machinery
dropped (this package is hardware-only; the serial TEST protocol is the whole
interface). Used by test_device/ (pytest) and usable interactively.
"""

import glob
import threading
import time

import serial  # pyserial

# Substrings that indicate the firmware crashed — any of these in the log fails a test.
CRASH_MARKERS = [
    "rst:0x",
    "Guru Meditation",
    "guru meditation",
    "panic'ed",
    "Panic",
    "abort() was called",
    "CORRUPT HEAP",
    "Stack canary",
    "LoadProhibited",
    "StoreProhibited",
    "InstrFetchProhibited",
    "Backtrace:",
]


def find_port():
    """First likely ESP32-S3 USB-CDC serial device, or None."""
    ports = sorted(
        glob.glob("/dev/cu.usbmodem*")
        + glob.glob("/dev/ttyACM*")
        + glob.glob("/dev/ttyUSB*")
    )
    return ports[0] if ports else None


class DeviceHarness:
    """Background serial reader + timestamped ring buffer + TEST-protocol helpers."""

    def __init__(self, port=None, baud=115200):
        self.port = port or find_port()
        if not self.port:
            raise RuntimeError("no serial device found")
        self.ser = serial.Serial(self.port, baud, timeout=0.2)
        self._lines = []  # list[(monotonic_ts, line)]
        self._lock = threading.Lock()
        self._stop = False
        self._t = threading.Thread(target=self._reader, daemon=True)
        self._t.start()

    def _reader(self):
        while not self._stop:
            try:
                raw = self.ser.readline()
            except Exception:
                break
            if raw:
                line = raw.decode("utf-8", "replace").rstrip()
                with self._lock:
                    self._lines.append((time.monotonic(), line))
                    if len(self._lines) > 20000:
                        self._lines = self._lines[-10000:]

    def now(self):
        return time.monotonic()

    def snapshot(self, since=0.0):
        with self._lock:
            return [ln for (ts, ln) in self._lines if ts >= since]

    def send(self, cmd):
        self.ser.write((cmd + "\n").encode())

    def wait_for(self, substr, timeout=10.0, since=0.0):
        """Block until a serial line contains `substr`; return the line or None."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for ln in self.snapshot(since):
                if substr in ln:
                    return ln
            time.sleep(0.05)
        return None

    def assert_no_crash(self, since=0.0):
        for ln in self.snapshot(since):
            for m in CRASH_MARKERS:
                if m in ln:
                    raise AssertionError(f"crash marker '{m}' in serial: {ln}")

    def reset(self):
        """Pulse DTR/RTS to reboot the board (auto-reset circuit)."""
        self.ser.setDTR(False)
        self.ser.setRTS(True)
        time.sleep(0.1)
        self.ser.setRTS(False)
        time.sleep(0.5)

    def close(self):
        self._stop = True
        time.sleep(0.3)
        try:
            self.ser.close()
        except Exception:
            pass
