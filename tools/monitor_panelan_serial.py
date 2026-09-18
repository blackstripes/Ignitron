#!/usr/bin/env python3
"""Print PanelLan USB-serial output for a bounded hardware observation window."""

import argparse
import pathlib
import sys
import time

import serial


parser = argparse.ArgumentParser()
parser.add_argument("--port", required=True)
parser.add_argument("--duration", type=float, default=30.0)
parser.add_argument("--output", help="optional file to receive the captured serial text")
args = parser.parse_args()

output = pathlib.Path(args.output).open("w") if args.output else sys.stdout
port = serial.Serial(args.port, 115200, timeout=0.25)
try:
    deadline = time.monotonic() + args.duration
    while time.monotonic() < deadline:
        data = port.read(4096)
        if data:
            output.write(data.decode(errors="replace"))
            output.flush()
finally:
    port.close()
    if args.output:
        output.close()
