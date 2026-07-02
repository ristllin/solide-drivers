#!/usr/bin/env python3
"""Continuous, auto-reconnecting serial console/recorder for solide-drivers.

Timestamps every line and appends to solide_console.log (repo root), reconnecting
across resets/flashes so you can poke at the hardware at your own pace. Optionally
drives one TEST command on connect.

    python tools/solide_console.py              # just record
    python tools/solide_console.py --test all   # send 'TEST all' ~2 s after connect
"""
import glob
import os
import sys
import time

import serial  # pyserial


def find_port():
    ps = sorted(
        glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*")
    )
    return ps[0] if ps else None


def main():
    test_cmd = None
    if "--test" in sys.argv:
        i = sys.argv.index("--test")
        if i + 1 < len(sys.argv):
            test_cmd = sys.argv[i + 1]

    logpath = os.path.join(os.path.dirname(__file__), "..", "solide_console.log")
    ser = None
    with open(logpath, "a", buffering=1) as f:
        f.write("\n==== console %s ====\n" % time.strftime("%H:%M:%S"))
        sent = False
        connected_at = 0.0
        while True:
            if ser is None:
                port = find_port()
                if not port:
                    time.sleep(0.3)
                    continue
                try:
                    ser = serial.Serial(port, 115200, timeout=0.5)
                except Exception:
                    ser = None
                    time.sleep(0.3)
                    continue
                f.write("[connected %s]\n" % port)
                sent = False
                connected_at = time.time()
            try:
                if test_cmd and not sent and time.time() - connected_at > 2.0:
                    ser.write(("TEST %s\n" % test_cmd).encode())
                    sent = True
                line = ser.readline()
                if line:
                    txt = "%s %s" % (time.strftime("%H:%M:%S"),
                                     line.decode("utf-8", "replace").rstrip())
                    print(txt)
                    f.write(txt + "\n")
            except Exception:
                try:
                    ser.close()
                except Exception:
                    pass
                ser = None
                time.sleep(0.3)


if __name__ == "__main__":
    main()
