#!/usr/bin/env python3
"""Operator-only PanelLan FX acceptance run; no auto-detection or flashing."""

import argparse
import json
import os
import re
import select
import termios
import time


SLOTS = ("gate", "comp", "drive", "mod", "delay", "reverb")
SEND = re.compile(r"Controller: sending FX ([0-5]) \(([^()]+)\) (on|off)")
CONFIRM = re.compile(r"Controller: FX ([0-5]) \(([^()]+)\) confirmed by (FX_ONOFF|full preset response)")
CANCEL = re.compile(r"Controller: FX ([0-5]) cancelled: (.+)")
REJECT_FX = "Effect command failed or was rejected."
REJECT_PRESET = "Preset request rejected; wait for synchronization to finish."


class Unresolved(RuntimeError):
    """Never send another toggle after an ambiguous or failed action."""


class NotReady(Unresolved):
    """A request was rejected before any FX command reached the amp."""


def status_is_ready(lines):
    values = {}
    for line in lines:
        for label in ("Spark connected", "Amp", "Serial", "Preset name"):
            prefix = label + ":"
            if line.startswith(prefix):
                values[label] = line[len(prefix):].strip()
    return (values.get("Spark connected") == "yes" and
            all(values.get(k) not in (None, "", "(unknown)") for k in ("Amp", "Serial", "Preset name")))


class SerialLines:
    def __init__(self, port):
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        try:
            self.original = termios.tcgetattr(self.fd)
            config = termios.tcgetattr(self.fd)
            config[0] = config[1] = config[3] = 0
            config[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
            config[4] = config[5] = termios.B115200
            config[6][termios.VMIN] = config[6][termios.VTIME] = 0
            termios.tcsetattr(self.fd, termios.TCSANOW, config)
        except Exception:
            os.close(self.fd)
            raise
        self.buf = b""

    def close(self):
        try:
            termios.tcsetattr(self.fd, termios.TCSANOW, self.original)
        finally:
            os.close(self.fd)

    def write(self, command):
        print(f"> {command}", flush=True)
        data = (command + "\n").encode()
        while data:
            _, writable, _ = select.select([], [self.fd], [], 1)
            if not writable:
                raise Unresolved("serial write timed out")
            data = data[os.write(self.fd, data):]

    def read(self, deadline):
        while time.monotonic() < deadline:
            if b"\n" in self.buf:
                raw, self.buf = self.buf.split(b"\n", 1)
                line = raw.decode(errors="replace").strip()
                print(line, flush=True)
                return line
            readable, _, _ = select.select([self.fd], [], [], min(0.1, max(0, deadline - time.monotonic())))
            if readable:
                try:
                    data = os.read(self.fd, 4096)
                except BlockingIOError:
                    continue
                if not data:
                    raise Unresolved("serial device closed")
                self.buf += data
        return None


def wait_ready(io, timeout):
    deadline = time.monotonic() + timeout
    next_status = 0
    lines = None
    while time.monotonic() < deadline:
        if time.monotonic() >= next_status:
            io.write("status")
            next_status = time.monotonic() + 1
        line = io.read(min(deadline, next_status))
        if line is None:
            continue
        if (SEND.search(line) or CONFIRM.search(line) or CANCEL.search(line) or
                line == "Effect toggle requested." or
                re.search(r"PRESET_TRACE .*event=accept\b", line)):
            raise Unresolved(f"unexpected action while waiting for readiness: {line}")
        if line == "--- Ignitron status ---":
            lines = []
        elif lines is not None:
            lines.append(line)
            if line.startswith("Looper loops:"):
                if status_is_ready(lines):
                    # CLI status can print before ControllerState consumes the
                    # just-parsed startup full-preset observation. Let one
                    # controller iteration publish Ready before first action.
                    time.sleep(0.25)
                    return
                lines = None
    raise Unresolved("timed out waiting for connected amp identity and preset data; no FX sent")


class Step:
    def __init__(self, slot, probes=False, preset=None, inverse_of=None):
        self.slot = slot
        self.probes = probes
        self.preset = preset if probes else None
        self.inverse_of = inverse_of
        self.model = self.direction = self.source = None
        self.fx_rejected = False
        self.preset_rejected = self.preset is None
        self.preset_trace = self.preset is None
        self.confirmed = False
        self.observed = {}
        self.payload = None

    def observe_payload(self, line):
        if line == "Message processed:":
            self.payload = ""
            return
        if self.payload is None:
            return
        self.payload += line + "\n"
        try:
            data = json.loads(self.payload)
        except json.JSONDecodeError:
            return  # full preset JSON spans several serial lines
        self.payload = None
        if not isinstance(data, dict) or self.model is None:
            return
        if data.get("Effect") == self.model and type(data.get("IsOn")) is bool:
            self.observed["FX_ONOFF"] = data["IsOn"]
        pedals = data.get("Pedals")
        if isinstance(pedals, list):
            matches = [p["IsOn"] for p in pedals if isinstance(p, dict) and
                       p.get("Name") == self.model and type(p.get("IsOn")) is bool]
            if len(matches) == 1:
                self.observed["full preset response"] = matches[0]

    def observe(self, line):
        """Consume only lines arriving after this step's command; fail closed."""
        self.observe_payload(line)
        send = SEND.search(line)
        confirmation = CONFIRM.search(line)
        cancel = CANCEL.search(line)
        if cancel:
            raise Unresolved(f"FX cancelled: {line}")
        if line == "Spark amp is not connected." or line.startswith("Current preset data is not available"):
            raise Unresolved(line)
        if send:
            if self.model is not None or int(send[1]) != self.slot or self.confirmed:
                raise Unresolved(f"unexpected/duplicate FX send: {line}")
            self.model, self.direction = send[2], send[3]
            if self.inverse_of is not None and (self.model != self.inverse_of.model or
                                                self.direction == self.inverse_of.direction):
                raise Unresolved("second FX request did not target the same model in the inverse direction")
        if confirmation:
            if (self.model is None or int(confirmation[1]) != self.slot or
                    confirmation[2] != self.model or self.confirmed):
                raise Unresolved(f"uncorrelated FX confirmation: {line}")
            if self.probes and not self.probes_passed:
                raise Unresolved("FX confirmed before busy probes were demonstrably rejected; inconclusive")
            self.source = confirmation[3]
            expected = self.direction == "on"
            if (self.observed.get(self.source) is not expected or
                    any(value is not expected for value in self.observed.values())):
                raise Unresolved(f"FX confirmation lacks consistent Spark IsOn={expected}: {line}")
            self.confirmed = True
        if REJECT_FX in line:
            if self.model is None:
                raise NotReady(f"FX request rejected before send: {line}")
            if not self.probes or self.fx_rejected or self.confirmed:
                raise Unresolved(f"unexpected FX rejection: {line}")
            self.fx_rejected = True
        if "Effect toggle requested." in line and self.model is not None and self.probes:
            raise Unresolved("busy duplicate FX toggle was accepted")
        if REJECT_PRESET in line:
            if self.preset is None or self.preset_rejected or self.confirmed:
                raise Unresolved(f"unexpected preset rejection: {line}")
            self.preset_rejected = True
        trace_start = line.find("PRESET_TRACE ")
        if trace_start >= 0 and self.preset is not None:
            fields = dict(re.findall(r"(\w+)=([^\s]+)", line[trace_start:]))
            if fields.get("event") == "accept":
                raise Unresolved("preset command accepted while FX pending")
            if fields.get("target") == str(self.preset):
                if fields.get("event") == "reject" and fields.get("reason") == "fx" and not self.confirmed:
                    self.preset_trace = True
        if re.search(r"Preset \d+ (requested|selected)", line):
            raise Unresolved("unexpected preset change")

    @property
    def probes_passed(self):
        return self.fx_rejected and self.preset_rejected and self.preset_trace


def run_step(io, slot, timeout, probes=False, preset=None, inverse_of=None):
    step = Step(slot, probes, preset, inverse_of)
    io.write(f"fx {SLOTS[slot]} toggle")
    deadline = time.monotonic() + timeout
    while not step.confirmed:
        line = io.read(deadline)
        if line is None:
            raise Unresolved(f"FX {slot} timed out; no further toggles sent")
        step.observe(line)
        if step.model is not None and probes and not getattr(step, "probes_sent", False):
            # Do not probe before the first send, or a rejected initial command
            # could be mistaken for a rejected duplicate.
            step.probes_sent = True
            io.write(f"fx {SLOTS[slot]} toggle")
            if preset is not None:
                io.write(f"preset {preset}")
    return step


def run_confirmed_step(io, slot, timeout, ready_timeout, probes=False, preset=None, inverse_of=None):
    # A CLI rejection before ControllerActions sends has no amp-side effect.
    # Wait for startup sync and retry that same request; never retry after a send.
    for attempt in range(3):
        try:
            return run_step(io, slot, timeout, probes, preset, inverse_of)
        except NotReady:
            if attempt == 2:
                raise Unresolved(f"FX slot {slot} remained unavailable after sync retries")
            print(f"FX slot {slot} rejected before send; waiting for startup sync", flush=True)
            time.sleep(2.1)
            wait_ready(io, ready_timeout)
    raise Unresolved(f"FX slot {slot} was not confirmed")


def run(io, timeout, ready_timeout, probes, preset):
    wait_ready(io, ready_timeout)
    for slot, name in enumerate(SLOTS):
        # No second toggle unless the first is positively confirmed. A failed
        # second toggle leaves state uncertain; stop instead of compensating.
        first = run_confirmed_step(io, slot, timeout, ready_timeout, probes, preset)
        wait_ready(io, ready_timeout)
        second = run_confirmed_step(io, slot, timeout, ready_timeout, inverse_of=first)
        print(f"PASS {name} slot={slot} model={first.model} {first.direction}/{second.direction} "
              f"confirmed={first.source}/{second.source}", flush=True)
        if slot != len(SLOTS) - 1:
            wait_ready(io, ready_timeout)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="PanelLan USB CDC device (115200 baud)")
    parser.add_argument("--timeout", type=float, default=18, help="seconds per FX action (default 18)")
    parser.add_argument("--ready-timeout", type=float, default=30)
    parser.add_argument("--no-busy-probes", action="store_true")
    parser.add_argument("--probe-preset", type=int, metavar="N", help="also reject preset N (1..8) while FX pending; trace firmware required")
    args = parser.parse_args()
    if args.timeout <= 0 or args.ready_timeout <= 0 or (args.probe_preset is not None and not 1 <= args.probe_preset <= 8):
        parser.error("timeouts must be positive; probe preset must be 1..8")
    if args.no_busy_probes and args.probe_preset is not None:
        parser.error("--probe-preset requires busy probes")
    io = SerialLines(args.port)
    try:
        run(io, args.timeout, args.ready_timeout, not args.no_busy_probes, args.probe_preset)
    except (Unresolved, OSError) as exc:
        raise SystemExit(f"FAIL/INCONCLUSIVE: {exc}") from exc
    finally:
        io.close()
    print("PASS: six slots, two confirmed inverse toggles each", flush=True)


if __name__ == "__main__":
    main()
