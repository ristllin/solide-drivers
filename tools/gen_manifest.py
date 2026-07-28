#!/usr/bin/env python3
"""Regenerate docs/manifest.json — pins are parsed from the canonical board header
so they can never drift from the code. Static metadata (buses, volts, timings,
namespaces) lives here. Usage: python tools/gen_manifest.py
"""
import json
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HDR = os.path.join(ROOT, "include", "solide", "boards", "board_solide_s3.h")
OUT = os.path.join(ROOT, "docs", "manifest.json")


def parse_pins():
    """Extract {label: gpio} for each peripheral group from the Board literal."""
    text = open(HDR).read()
    groups = {}
    for grp in ("sd", "epd", "led", "enc", "spk", "mic", "tft"):
        m = re.search(r"/\*\s*%s\s*\*/\s*\{(.*?)\}" % grp, text, re.S)
        if not m:
            continue
        # -?\d+ so a not-connected pin (-1, e.g. the TFT's tirq) is reported as
        # -1 rather than silently dropped from the manifest.
        pairs = re.findall(r"/\*\s*(\w+)\s*\*/\s*(-?\d+)", m.group(1))
        groups[grp] = {k: int(v) for k, v in pairs}
    return groups


def build(p):
    return {
        "package": "solide-drivers",
        "device": "Solide S3",
        "board": "ESP32-S3-DevKitC-1 N16R8",
        "mcu": "ESP32-S3",
        "flash_mb": 16, "psram_mb": 8, "psram_type": "octal",
        "toolchain": {
            "platform": "pioarduino platform-espressif32 55.03.39",
            "arduino_esp32": "3.3.9", "esp_idf": "5.5.4", "framework": "arduino",
        },
        "reserved_gpio": {
            "strapping": [0, 45, 46], "usb": [19, 20], "uart0": [43, 44],
            "flash": [26, 27, 28, 29, 30, 31, 32], "octal_psram": [33, 34, 35, 36, 37],
            "onboard_rgb_repurposed_as_encoder_sw": [48],
        },
        "free_gpio": [3, 4, 5, 6, 9, 18],
        "peripherals": {
            "sd": {"namespace": "solide::storage", "bus": "FSPI/SPI2", "pins": p["sd"], "volts": 3.3, "fs": "FAT32"},
            "display": {"namespace": "solide::display", "bus": "HSPI/SPI3", "pins": p["epd"], "volts": 3.3,
                        "panel": "WeAct 2.9in 3-colour SSD1680",
                        "refresh_ms": {"bw_fast": 2200, "colour_3c": 18500}},
            "led": {"namespace": "solide::leds", "chip": "WS2812B", "pins": {"din": p["led"]["din"]},
                    "count": p["led"]["count"], "logic_v": 3.3, "power_v": 5,
                    "worst_case_mA_at_brightness_30": 372},
            "encoder": {"namespace": "solide::input", "type": "EC11", "pins": p["enc"], "volts": 3.3,
                        "steps_per_detent": 4},
            "speaker": {"namespace": "solide::audio", "bus": "I2S-std-TX (I2S1)", "amp": "NS4168",
                        "pins": p["spk"], "volts": 3.3, "note": "amp needs the 5 V bus for volume"},
            "mic": {"namespace": "solide::audio", "bus": "I2S-PDM-RX (I2S0)", "pins": p["mic"], "volts": 3.3,
                    "format": "16 kHz / 16-bit / mono",
                    "caveat": "shared VCC 3.3 V ONLY; 5 V damages the S3 (mic DATA follows VCC)"},
            # Alternative display: fitted INSTEAD of the e-paper, and it consumes
            # the encoder pins (1/2/48), so a TFT board has no knob — touch is the
            # input device. Firmware NVS (screenModel) selects which pair binds.
            "display_tft": {"namespace": "solide::display_tft", "bus": "HSPI/SPI3 (shared with touch)",
                            "pins": p["tft"], "volts": 3.3,
                            "panel": "2.8in ILI9341 240x320 RGB565",
                            "alternative_to": "display", "releases": "encoder",
                            "note": "tirq -1 = polled; bl is PWM (backlight is the idle draw)"},
            "touch": {"namespace": "solide::touch", "chip": "XPT2046", "bus": "HSPI/SPI3 (shared with display_tft)",
                      "pins": {"tcs": p["tft"]["tcs"], "tirq": p["tft"]["tirq"]}, "volts": 3.3,
                      "max_spi_hz": 2000000,
                      "caveat": "far slower than the panel's 40 MHz — needs its own SPI transaction settings"},
            "memory": {"namespace": "solide::memory",
                       "backends": ["NVS (typed key-value, <=15-char keys)", "SD JSON/blob under /memory/"]},
        },
        "namespaces": ["solide::display", "solide::display_tft", "solide::touch",
                       "solide::leds", "solide::ring", "solide::audio",
                       "solide::storage", "solide::memory", "solide::input", "solide::menu",
                       "solide::art", "solide::selftest"],
        "power": {
            "source": "USB-C 2S BMS -> 2x18650 series -> DC-DC -> 5 V bus",
            "rails": {"3v3": ["esp32-s3", "display", "sd", "audio board (amp + mic)"],
                      "5v": ["led ring", "audio amp volume headroom"]},
        },
        "build": {"example": "SOLIDE_EXAMPLE=<name> pio run -e esp32s3 -t upload",
                  "native_tests": "pio test -e native"},
        "serial_protocol": {"command": "TEST <led|epd|sd|memory|input|audio|all>",
                            "reply": "RESULT <name> PASS|FAIL|SKIP <k=v>...",
                            "info": "INFO -> INFO board=... psramMB=... heap=... uptime=..."},
    }


if __name__ == "__main__":
    manifest = build(parse_pins())
    with open(OUT, "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    print("wrote", OUT)
