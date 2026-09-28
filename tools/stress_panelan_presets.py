#!/usr/bin/env python3
"""Manual serial bench test; never opens hardware without an explicit --port."""
import argparse
import os
import re
import select
import termios
import time


def record_trace(text, records, outcomes):
    if not text.startswith("PRESET_TRACE "):
        return False
    fields = dict(re.findall(r"(\w+)=([^\s]+)", text))
    event = fields.get("event")
    if event in ("accept", "reject"):
        outcomes.append(fields)
        if fields.get("id"):
            records.setdefault("_outcome_ids", set()).add(fields["id"])
        records.setdefault("_timeline", []).append(fields)
    elif event == "queued" and fields.get("id") not in records.setdefault("_outcome_ids", set()):
        # USB CDC may drop an earlier accept trace line during very rapid input,
        # but queued is emitted from the same accepted request and carries its id.
        accepted = dict(fields)
        accepted["event"] = "accept"
        outcomes.append(accepted)
        records["_outcome_ids"].add(fields["id"])
        records.setdefault("_timeline", []).append(accepted)
    if event == "number":
        records.setdefault("_timeline", []).append(fields)
    if "id" in fields and event in ("accept", "sent", "number_confirm", "satisfied", "fail"):
        records.setdefault(fields["id"], {}).setdefault(event, fields)
    return True


def final_confirmed_target(targets, records, outcomes):
    # Each command has exactly one accept/reject outcome, in command order.
    # Never let an earlier command to the same slot stand in for the last one.
    if len(outcomes) != len(targets):
        return None
    last = outcomes[-1]
    if last.get("target") != str(targets[-1]):
        return None
    noop = last.get("event") == "reject" and last.get("reason") == "already_current"
    if last.get("event") != "accept" and not noop:
        return None
    events = records.get(last.get("id"), {})
    observed = noop and last.get("confirmed") == str(targets[-1]) or (
        last.get("event") == "accept" and
        ("number_confirm" in events or "satisfied" in events) and "fail" not in events)
    if not observed:
        return None
    # A later Spark number supersedes an earlier action confirmation (or an
    # already-current no-op). Use arrival order, not trace timestamps/IDs.
    timeline = records.get("_timeline", [])
    after_last = next((timeline[i + 1:] for i in range(len(timeline) - 1, -1, -1)
                       if timeline[i] is last), [])
    numbers = [event for event in after_last if event["event"] == "number"]
    if numbers and numbers[-1].get("confirmed") != str(targets[-1]):
        return None
    return targets[-1]


def status_is_ready(lines):
    values = {}
    for line in lines:
        for label in ("Spark connected", "Amp", "Serial", "Preset name"):
            prefix = label + ":"
            if line.startswith(prefix):
                values[label] = line[len(prefix):].strip()
    return (values.get("Spark connected") == "yes" and
            values.get("Amp") not in (None, "(unknown)") and
            values.get("Serial") not in (None, "(unknown)") and
            values.get("Preset name") not in (None, "(unknown)"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="trace-firmware USB serial device")
    parser.add_argument("--sequence", default="2,3,4,2,3,2", help="comma-separated HW slots")
    parser.add_argument("--interval", type=float, default=0.15, help="seconds between commands")
    parser.add_argument("--settle", type=float, default=12, help="seconds to observe final confirmation")
    parser.add_argument("--ready-timeout", type=float, default=30,
                        help="seconds to wait for BLE identity and current preset before testing")
    args = parser.parse_args()
    targets = [int(x) for x in args.sequence.split(",")]
    if (not targets or any(x < 1 or x > 8 for x in targets) or args.interval < 0 or
            args.settle <= 0 or args.ready_timeout <= 0):
        parser.error("slots must be 1..8; interval >= 0; settle and ready-timeout > 0")
    fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    original = termios.tcgetattr(fd)
    records = {}
    outcomes = []
    buf = b""
    try:
        config = termios.tcgetattr(fd)
        config[0] = config[1] = config[3] = 0
        config[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        config[4] = config[5] = termios.B115200
        config[6][termios.VMIN] = 0
        config[6][termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW, config)
        # Opening CDC can reset firmware. Give the operator time to reach Ready.
        print("Waiting for connected amp identity and current preset", flush=True)
        ready_deadline = time.monotonic() + args.ready_timeout
        next_status = 0.0
        status_lines = []
        ready = False
        while time.monotonic() < ready_deadline and not ready:
            now = time.monotonic()
            if now >= next_status:
                os.write(fd, b"status\n")
                next_status = now + 1.0
            readable, _, _ = select.select([fd], [], [], 0.1)
            if not readable:
                continue
            try:
                buf += os.read(fd, 4096)
            except BlockingIOError:
                continue
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode(errors="replace").strip()
                record_trace(text, records, outcomes)
                if text == "--- Ignitron status ---":
                    status_lines = [text]
                elif status_lines:
                    status_lines.append(text)
                    if text.startswith("Looper loops:"):
                        ready = status_is_ready(status_lines)
                        status_lines = []
        if not ready:
            raise SystemExit("Timed out waiting for connected amp and current preset; sent no preset test commands")
        print("Controller is connected with preset data; starting sequence", flush=True)
        start = time.monotonic()
        next_send = start
        end = start + len(targets) * args.interval + args.settle
        sent_count = 0
        while time.monotonic() < end:
            now = time.monotonic()
            if sent_count < len(targets) and now >= next_send:
                target = targets[sent_count]
                os.write(fd, f"preset {target}\n".encode())
                print(f"command step={sent_count + 1} target={target}", flush=True)
                sent_count += 1
                next_send = now + args.interval
            readable, _, _ = select.select([fd], [], [], min(0.1, max(0, end - time.monotonic())))
            if not readable:
                continue
            try:
                buf += os.read(fd, 4096)
            except BlockingIOError:
                continue
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode(errors="replace").strip()
                if not record_trace(text, records, outcomes):
                    continue
                print(text, flush=True)
    finally:
        termios.tcsetattr(fd, termios.TCSANOW, original)
        os.close(fd)
    print(f"rejects={sum(o.get('event') == 'reject' for o in outcomes)} final_target={targets[-1]}")
    final = final_confirmed_target(targets, records, outcomes)
    for id_, events in records.items():
        if id_.startswith("_"):
            continue
        accepted = events.get("accept")
        if not accepted:
            continue
        target = accepted["target"]
        confirmed = events.get("number_confirm")
        sent = events.get("sent")
        observed = events.get("number_confirm") or events.get("satisfied")
        if confirmed:
            latency = (int(confirmed["t"]) - int(accepted["t"])) & 0xffffffff
        else:
            latency = "-"
        print(f"id={id_} target={target} sent={bool(sent)} observed={bool(observed)} latency_ms={latency} failed={bool(events.get('fail'))}")
    print(f"final_confirmed_target={final or 'none'}")


if __name__ == "__main__":
    main()
