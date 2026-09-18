#!/usr/bin/env python3
"""Capture `screenshot` output from the PanelLan LVGL firmware as a PPM file."""

import argparse
import pathlib
import serial
import time


parser = argparse.ArgumentParser()
parser.add_argument("--port", required=True)
parser.add_argument("--output", required=True)
args = parser.parse_args()

port = serial.Serial(args.port, 115200, timeout=0.5)
try:
    port.reset_input_buffer()
    port.write(b"screenshot\n")
    marker = b"IGNITRON_SCREENSHOT_PPM 320 240\n"
    data = bytearray()
    deadline = time.monotonic() + 35
    while marker not in data and time.monotonic() < deadline:
        data.extend(port.read(4096))
    start = data.find(marker)
    if start < 0:
        raise RuntimeError("Screenshot marker was not received")
    payload = bytearray(data[start + len(marker):])
    ppm_header = b"P6\n320 240\n255\n"
    while len(payload) < len(ppm_header) + 320 * 240 * 3 and time.monotonic() < deadline:
        payload.extend(port.read(4096))
    expected = len(ppm_header) + 320 * 240 * 3
    if payload[:len(ppm_header)] != ppm_header or len(payload) < expected:
        raise RuntimeError("Screenshot payload was incomplete")
    pathlib.Path(args.output).write_bytes(payload[:expected])
finally:
    port.close()
