#!/usr/bin/env python3
"""Event-driven Spark 2 preset diagnostic; requires an explicit serial port."""
import argparse
from collections import deque
from datetime import datetime
import os
import re
import select
import termios
import time


def trace(line):
    if not line.startswith("PRESET_TRACE "):
        return None
    return dict(re.findall(r"(\w+)=([^\s]+)", line))


def status(lines):
    """Parse one complete CLI status block (not a mixture of successive polls)."""
    if not lines or lines[0] != "--- Ignitron status ---" or not lines[-1].startswith("Looper loops:"):
        return None
    values = {}
    for line in lines:
        for key in ("Spark connected", "Amp", "Serial", "Preset name"):
            if line.startswith(key + ":"):
                values[key] = line[len(key) + 1:].strip()
        preset = re.match(r"Bank:\s*(-?\d+)\s+Preset:\s*(-?\d+)$", line)
        if preset:
            values["Current preset"] = preset.group(2)
    return values


def identity_ready(values):
    return (values.get("Spark connected") == "yes" and values.get("Amp") == "Spark 2" and
            all(values.get(key) not in (None, "", "(unknown)") for key in
                ("Serial", "Preset name", "Current preset")) and
            values.get("Current preset") in {str(slot) for slot in range(1, 9)})


class Soak:
    """Hardware-independent event state machine. All deadlines use the caller's monotonic clock."""
    def __init__(self, count=150, cadence=3.0, timeout=15.0):
        self.count, self.cadence, self.timeout = count, cadence, timeout
        self.started = False
        self.status_ready = False
        self.status_preset = None
        self.startup_query = None
        self.startup_ready = False
        self.current = None
        self.accepted = 0
        self.last_ready = None
        self.next_slot = 1
        self.action = None
        self.full_msg = None
        self.parsed = None
        self.events = deque(maxlen=50)
        self.context = deque(maxlen=20)
        self.stop = None
        self.last_send = float('-inf')

    def fail(self, reason):
        if self.stop is None:
            if self.parsed and not self.parsed["publish"] and self.action:
                reason += f"; parsed current full response without observation publish: {self.parsed}"
            self.stop = reason

    def on_status(self, values):
        if values.get("Amp") not in (None, "", "(unknown)", "Spark 2"):
            self.fail("wrong amp: " + values["Amp"])
        else:
            self.status_ready = identity_ready(values)
            self.status_preset = values.get("Current preset")
            if self.started and not self.status_ready:
                self.fail("Spark connection/identity/readiness lost in status")
            if self.status_ready and self.startup_ready:
                self.started = self.status_preset == self.current

    def send_due(self, now):
        if self.stop or not self.started or self.action or self.accepted >= self.count:
            return None
        if now - self.last_send < self.cadence:
            return None
        slot = self.next_slot
        self.next_slot = slot % 8 + 1
        self.action = {"target": str(slot), "id": None, "at": now}
        self.last_send = now
        self.full_msg = self.parsed = None
        return slot

    def tick(self, now):
        if self.action and now - self.action["at"] >= self.timeout:
            self.fail(f"diagnostic action timeout (no matching Ready/outcome); "
                      f"full_msg={self.full_msg} parsed={self.parsed}")

    def require(self, event, *keys):
        missing = [key for key in keys if key not in event]
        if missing:
            self.fail(f"missing trace fields {','.join(missing)} in {event.get('event', '?')}")
            return False
        return True

    def on_line(self, line):
        self.context.append(line)
        e = trace(line)
        if e is None:
            return
        self.events.append(line)
        kind = e.get("event")
        if kind == "ingress_trace_lost":
            self.fail(f"trace evidence lost: {e}")
        if kind == "disconnect":
            self.startup_ready = False
            self.startup_query = None
            self.fail("Spark disconnected")
        if not self.started and kind == "startup_full_query" and e.get("id") == "0":
            if self.require(e, "target", "sent", "msg"):
                self.startup_query = (e["target"], e["msg"]) if e["sent"] == "1" else None
                self.startup_ready = False
                if e["sent"] != "1":
                    self.fail(f"startup_full_query sent=0 (submission/owner unavailable): {e}")
                elif e.get("retry") == "1":
                    self.fail(f"startup_full_query retry: {e}")
        if not self.started and kind == "startup_full_timeout" and self.startup_query:
            if self.require(e, "target", "msg") and (e["target"], e["msg"]) == self.startup_query:
                self.fail("startup full timeout: " + str(e))
        if not self.started and kind == "ready" and e.get("id") == "0" and self.startup_query:
            if self.require(e, "target", "msg") and (e["target"], e["msg"]) == self.startup_query:
                self.startup_ready = True
                self.current = e["target"]
                # Wait for a complete status poll whose reported active slot
                # agrees with this current-link full-preset Ready observation.
                self.started = self.status_ready and self.current == self.status_preset
        a = self.action
        if not a or self.stop:
            return
        target, id_ = a["target"], a["id"]
        if kind == "reject" and e.get("target") == target and id_ is None:
            if not self.require(e, "reason", "confirmed"):
                return
            if e["reason"] == "already_current" and e["confirmed"] == target:
                self.current = target
                self.action = None  # No-op is not a changing action.
                self.full_msg = None
            else:
                self.fail("preset rejected: " + str(e))
            return
        if kind == "accept" and e.get("target") == target and id_ is None:
            if self.require(e, "id", "confirmed"):
                a["id"] = e["id"]
                if e["confirmed"] == target:
                    self.fail("accepted action not changing: " + str(e))
                else:
                    self.accepted += 1
            return
        if id_ is None:
            return
        owned = e.get("id") == id_ and e.get("target") == target
        if kind in ("fail", "full_data_timeout", "full_data_conflict", "startup_full_timeout") and owned:
            self.fail(f"{kind}: {e}; parsed={self.parsed}")
        if kind in ("full_query", "startup_full_query") and owned:
            if not self.require(e, "sent", "msg"):
                return
            if e["sent"] != "1":
                self.fail(f"{kind} sent=0 (submission/owner/currentCommand unavailable in firmware trace): {e}")
                return
            if e.get("retry") == "1":
                self.fail(f"{kind} retry: {e}")
                return
            self.full_msg, self.parsed = e["msg"], None  # retry supersedes prior query
        if kind == "number_confirm" and owned:
            if self.require(e, "confirmed") and e["confirmed"] != target:
                self.fail("number confirmation mismatch: " + str(e))
        # Only the current query's preset identity is relevant. Other messages,
        # including parsed replies for superseded queries, are not anomalies.
        matching = self.full_msg is not None and e.get("msg") == self.full_msg
        preset = e.get("cmd") in ("01", "03") and e.get("sub") == "01"
        if matching and kind in ("multipart_complete", "preset_parse_complete", "preset_parse_reject",
                                 "incoming_reject"):
            if not self.require(e, "cmd", "sub"):
                return
        if matching and preset:
            if kind == "multipart_complete":
                if self.require(e, "expected", "received", "reason"):
                    if e["expected"] != e["received"]:
                        self.fail("incomplete multipart_complete: " + str(e))
            elif kind == "preset_parse_reject":
                self.fail("current full preset parse rejected: " + str(e))
            elif kind == "incoming_reject":
                self.fail("current full response frame rejected: " + str(e))
            elif kind == "preset_parse_complete":
                if self.require(e, "slot", "frames", "bytes"):
                    self.parsed = {"parse": e, "gate": None, "publish": False}
        if matching and kind == "preset_apply_gate" and self.parsed:
            if self.require(e, "expected", "match", "owner_active", "owner_msg", "owner_sub",
                            "slot", "cache", "acceptedActive", "revision_before", "revision_after"):
                self.parsed["gate"] = e
        if matching and kind == "preset_observation_publish" and self.parsed:
            if self.require(e, "slot", "revision"):
                self.parsed["publish"] = True
        if matching and kind == "response_lane_complete" and preset and self.parsed and not self.parsed["publish"]:
            self.require(e, "active_before", "owner_msg", "owner_sub", "matched", "released")
            self.fail("parsed current full response did not publish before lane completion: " +
                      str(self.parsed) + "; lane=" + str(e))
        if matching and kind == "multipart_discard" and preset:
            if self.require(e, "expected", "received", "reason"):
                self.fail("current full response multipart anomaly: " + str(e))
        if matching and kind == "frame_discard" and preset:
            self.fail("current full response frame discarded: " + str(e))
        if kind == "ready" and owned:
            if not self.require(e, "msg"):
                return
            if e["msg"] != self.full_msg:
                self.fail("Ready without matching current full query: " + str(e))
            elif self.parsed is None:
                self.fail("Ready without current full parse trace: " + str(e))
            elif self.parsed and not self.parsed["publish"]:
                self.fail("parsed full response without observation publish: " + str(self.parsed))
            else:
                self.current = target
                self.last_ready = (id_, target)
                self.action = None
                self.full_msg = None
                if self.accepted >= self.count:
                    self.stop = "completed"


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", required=True, help="trace firmware CDC port (115200; opening may reset controller)")
    p.add_argument("--count", type=int, default=150)
    p.add_argument("--cadence", type=float, default=3.0, help="minimum seconds between commands")
    p.add_argument("--action-timeout", type=float, default=15.0)
    p.add_argument("--ready-timeout", type=float, default=30.0)
    args = p.parse_args()
    if not 1 <= args.count <= 150 or args.cadence < 3 or args.action_timeout <= 0 or args.ready_timeout <= 0:
        p.error("count must be 1..150, cadence >= 3, timeouts > 0 required")
    soak = Soak(args.count, args.cadence, args.action_timeout)
    logpath = f"/tmp/opencode/panelan_preset_diagnostic_{datetime.now().strftime('%Y%m%d_%H%M%S')}.log"
    fd = None
    original = None
    blocks = []
    buf = b""
    try:
        with open(logpath, "xb") as log:
            fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            original = termios.tcgetattr(fd)
            cfg = termios.tcgetattr(fd)
            cfg[0] = cfg[1] = cfg[3] = 0
            cfg[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
            cfg[4] = cfg[5] = termios.B115200
            cfg[6][termios.VMIN] = cfg[6][termios.VTIME] = 0
            termios.tcsetattr(fd, termios.TCSANOW, cfg)
            deadline = time.monotonic() + args.ready_timeout
            next_status = 0
            while not soak.stop:
                now = time.monotonic()
                if not soak.started:
                    if now >= deadline:
                        soak.fail("readiness timeout; zero preset commands sent")
                        break
                    if now >= next_status:
                        os.write(fd, b"status\n")
                        next_status = now + 1
                else:
                    soak.tick(now)
                    slot = soak.send_due(now)
                    if slot is not None:
                        os.write(fd, f"preset {slot}\n".encode())
                readable, _, _ = select.select([fd], [], [], 0.1)
                if not readable:
                    continue
                data = os.read(fd, 4096)
                if not data:
                    soak.fail("serial EOF")
                    break
                buf += data
                while b"\n" in buf:
                    raw, buf = buf.split(b"\n", 1)
                    raw_line = raw + b"\n"
                    log.write(datetime.now().isoformat(timespec="milliseconds").encode("ascii") + b" " + raw_line)
                    log.flush()
                    line = raw.decode(errors="replace").strip()
                    soak.on_line(line)
                    if line == "--- Ignitron status ---":
                        blocks = [line]
                    elif blocks:
                        blocks.append(line)
                        if line.startswith("Looper loops:"):
                            soak.on_status(status(blocks))
                            blocks = []
    except (OSError, termios.error) as exc:
        soak.fail(f"serial/log error: {exc}")
    finally:
        if buf:
            try:
                with open(logpath, "ab") as log:
                    log.write(datetime.now().isoformat(timespec="milliseconds").encode("ascii") + b" " + buf)
                    log.flush()
            except OSError:
                pass
        if fd is not None:
            if original is not None:
                termios.tcsetattr(fd, termios.TCSANOW, original)
            os.close(fd)
    print(f"stop={soak.stop} accepted_changing={soak.accepted} last_ready={soak.last_ready} "
          f"current={soak.action} full_msg={soak.full_msg} logfile={logpath}")
    print("Last 50 trace events:")
    print("\n".join(soak.events))
    print("Nearby raw context:")
    print("\n".join(soak.context))
    return 0 if soak.stop == "completed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
