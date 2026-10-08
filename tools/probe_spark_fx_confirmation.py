#!/usr/bin/env python3
"""Opt-in Spark FX wire probe. No FX/protocol write unless --mutate is supplied.

Requires an explicit BLE address; no discovery, retry, or state restoration.
Bleak's notify subscription may write the GATT CCCD; observe-only mode sends no
Spark command. JSONL preserves each notification even if its contents cannot be parsed.
"""

import argparse
import asyncio
from datetime import datetime, timezone
import json
from pathlib import Path
import time


SERVICE_UUID = "0000ffc0-0000-1000-8000-00805f9b34fb"
WRITE_UUID = "0000ffc1-0000-1000-8000-00805f9b34fb"
NOTIFY_UUID = "0000ffc2-0000-1000-8000-00805f9b34fb"
BLOCK_START = b"\x01\xfe\x00\x00\x41\xff"


def encode_7bit(data):
    encoded = bytearray()
    for offset in range(0, len(data), 7):
        group = data[offset:offset + 7]
        encoded.append(sum((byte >> 7) << bit for bit, byte in enumerate(group)))
        encoded.extend(byte & 0x7f for byte in group)
    return bytes(encoded)


def decode_7bit(data):
    decoded = bytearray()
    for offset in range(0, len(data), 8):
        group = data[offset:offset + 8]
        if len(group) < 2 or group[0] & 0x80 or any(byte & 0x80 for byte in group[1:]):
            raise ValueError("invalid 7-bit group")
        if group[0] >> (len(group) - 1):
            raise ValueError("unused mask bits set")
        decoded.extend(byte | (((group[0] >> bit) & 1) << 7)
                       for bit, byte in enumerate(group[1:]))
    return bytes(decoded)


def checksum(data):
    value = 0
    for byte in data:
        value ^= byte
    return value


def build_effect_command(effect, state, message_number):
    """One SparkMessage::turnEffectOnOff DIR_TO_SPARK block (no chunk splitting)."""
    if not 1 <= message_number <= 255 or state not in ("on", "off"):
        raise ValueError("invalid message number or state")
    try:
        name = effect.encode("ascii")
    except UnicodeEncodeError as exc:
        raise ValueError("effect must be ASCII") from exc
    if not name or len(name) > 95 or any(byte < 0x20 or byte > 0x7e for byte in name):
        raise ValueError("effect must be 1..95 printable ASCII bytes")
    payload = bytes((len(name), len(name) + 0xa0)) + name + bytes((0xc3 if state == "on" else 0xc2, 0))
    encoded = encode_7bit(payload)
    frame = bytes((0xf0, 1, message_number, checksum(encoded), 1, 0x15)) + encoded + b"\xf7"
    size = 16 + len(frame)
    if size > 0xad:
        raise ValueError("effect command exceeds single Spark block")
    return b"\x01\xfe\x00\x00\x53\xfe" + bytes((size,)) + bytes(9) + frame


def command_chunks(packet, max_write_without_response_size):
    """Fragment one Spark block for FFC1 write-without-response transport."""
    limit = max_write_without_response_size
    if isinstance(limit, bool) or not isinstance(limit, int) or limit <= 0:
        raise ValueError("FFC1 max_write_without_response_size is unavailable or invalid; refusing mutation")
    return [packet[offset:offset + limit] for offset in range(0, len(packet), limit)]


def parse_frame(raw):
    record = {"type": "frame", "hex": raw.hex()}
    if len(raw) < 7 or raw[:2] != b"\xf0\x01" or raw[-1] != 0xf7:
        record["error"] = "invalid frame"
        return record
    number, wire_sum, cmd, sub = raw[2:6]
    encoded = raw[6:-1]
    record.update(message_number=number, command=f"{cmd:02x}", subcommand=f"{sub:02x}",
                  checksum=f"{wire_sum:02x}", calculated_checksum=f"{checksum(encoded):02x}",
                  checksum_ok=wire_sum == checksum(encoded))
    if not record["checksum_ok"]:
        record["error"] = "checksum mismatch"
        return record
    try:
        payload = decode_7bit(encoded)
    except ValueError as exc:
        record["error"] = str(exc)
        return record
    record["payload_hex"] = payload.hex()
    if (cmd, sub) == (4, 0x15):
        record["event"] = "generic_ack"
    elif (cmd, sub) == (3, 0x15):
        record["event"] = "effect_state"
        if len(payload) < 4 or payload[0] == 0 or payload[1] != payload[0] + 0xa0 or len(payload) < payload[0] + 3:
            record["error"] = "malformed prefixed effect"
            return record
        length = payload[0]
        try:
            effect = payload[2:2 + length].decode("ascii")
        except UnicodeDecodeError:
            record["error"] = "invalid effect string"
            return record
        marker = payload[2 + length]
        if marker not in (0xc2, 0xc3):
            record["error"] = "invalid on/off marker"
            return record
        record.update(effect=effect, state="on" if marker == 0xc3 else "off",
                      trailing_hex=payload[3 + length:].hex())
    return record


class SparkParser:
    """Reassemble wrappers and F0 frames across notification boundaries."""

    def __init__(self):
        self.block = bytearray()
        self.frame = bytearray()
        self.prefix = bytearray()

    def feed(self, notification):
        events = []
        for byte in notification:
            events.extend(self._byte(byte))
        return events

    def _byte(self, byte):
        if self.block:
            self.block.append(byte)
            if len(self.block) == 7 and not 16 <= self.block[6] <= 0xad:
                event = {"type": "unparsed", "reason": "invalid block size", "hex": self.block.hex()}
                self.block.clear()
                return [event]
            if len(self.block) >= 7 and len(self.block) == self.block[6]:
                block = bytes(self.block)
                self.block.clear()
                return [{"type": "block", "hex": block.hex(), "size": len(block)}] + self._frames(block[16:])
            return []
        if self.prefix or byte == 1:
            self.prefix.append(byte)
            if BLOCK_START.startswith(self.prefix):
                if len(self.prefix) == len(BLOCK_START):
                    self.block = self.prefix
                    self.prefix = bytearray()
                return []
            # Not a wrapper: return buffered bytes to the pending frame, then
            # recheck the rest for another possible wrapper start.
            pending = bytes(self.prefix)
            self.prefix.clear()
            events = self._frames(pending[:1])
            for remaining in pending[1:]:
                events.extend(self._byte(remaining))
            return events
        return self._frames(bytes((byte,)))

    def _frames(self, data):
        events = []
        for byte in data:
            if not self.frame and byte != 0xf0:
                events.append({"type": "unparsed", "reason": "outside frame", "hex": bytes((byte,)).hex()})
                continue
            self.frame.append(byte)
            if len(self.frame) == 2 and self.frame[1] != 1:
                events.append({"type": "unparsed", "reason": "invalid frame start", "hex": self.frame.hex()})
                self.frame.clear()
            elif byte == 0xf7 and len(self.frame) >= 7:
                events.append(parse_frame(bytes(self.frame)))
                self.frame.clear()
            elif len(self.frame) > 1024:
                events.append({"type": "unparsed", "reason": "frame overflow", "hex": self.frame.hex()})
                self.frame.clear()
        return events


class Probe:
    def __init__(self, sink):
        self.sink = sink
        self.parser = SparkParser()
        self.sent_at = None
        self.message_number = None

    def record(self, item, wall=None, monotonic=None):
        item = dict(item)
        item.update(wall=wall or datetime.now(timezone.utc).isoformat(),
                    monotonic=time.monotonic() if monotonic is None else monotonic)
        self.sink.write(json.dumps(item) + "\n")
        self.sink.flush()
        return item

    def notification(self, _sender, data, *, wall=None, monotonic=None):
        mono = time.monotonic() if monotonic is None else monotonic
        wall = wall or datetime.now(timezone.utc).isoformat()
        self.record({"type": "notification", "hex": bytes(data).hex()}, wall, mono)
        for event in self.parser.feed(data):
            if (event.get("type") == "frame" and event.get("checksum_ok") and
                    not event.get("error") and event.get("event") in ("generic_ack", "effect_state") and
                    event["message_number"] == self.message_number and self.sent_at is not None and mono >= self.sent_at):
                event["latency_ms"] = (mono - self.sent_at) * 1000
            self.record(event, wall, mono)


def positive_duration(value):
    try:
        number = float(value)
        if not 0 < number <= 3600 or not number < float("inf"):
            raise ValueError
        return number
    except ValueError as exc:
        raise argparse.ArgumentTypeError("duration must be > 0 and <= 3600 seconds") from exc


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--address", required=True, help="explicit BLE address (no scanning)")
    parser.add_argument("--duration", type=positive_duration, default=15.0, help="seconds to listen (default 15)")
    parser.add_argument("--output", type=Path, help="JSONL path (default /tmp/opencode/spark_fx_probe_<timestamp>.jsonl)")
    parser.add_argument("--mutate", action="store_true", help="opt in to exactly one FX write; no restore")
    parser.add_argument("--effect")
    parser.add_argument("--state", choices=("on", "off"))
    parser.add_argument("--message-number", type=int)
    args = parser.parse_args(argv)
    if not args.address.strip():
        parser.error("--address cannot be empty")
    if args.mutate:
        if args.effect is None or args.state is None or args.message_number is None:
            parser.error("--mutate requires --effect, --state and --message-number")
        try:
            build_effect_command(args.effect, args.state, args.message_number)
        except ValueError as exc:
            parser.error(str(exc))
    elif any(value is not None for value in (args.effect, args.state, args.message_number)):
        parser.error("effect/state/message-number require --mutate")
    return args


async def run(args):
    # No Bleak import on parser/test paths; only the explicit live invocation reaches here.
    from bleak import BleakClient

    output = args.output or Path("/tmp/opencode") / ("spark_fx_probe_" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ") + ".jsonl")
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x") as sink:
        probe = Probe(sink)
        async with BleakClient(args.address) as client:
            service = client.services.get_service(SERVICE_UUID)
            if service is None:
                raise RuntimeError("explicit address does not expose Spark service FFC0")
            notify_char = service.get_characteristic(NOTIFY_UUID)
            if notify_char is None or "notify" not in notify_char.properties:
                raise RuntimeError("Spark notify characteristic FFC2 is unavailable")
            await client.start_notify(notify_char, probe.notification)
            if args.mutate:
                packet = build_effect_command(args.effect, args.state, args.message_number)
                characteristic = service.get_characteristic(WRITE_UUID)
                if characteristic is None or "write-without-response" not in characteristic.properties:
                    raise RuntimeError("Spark write characteristic FFC1 lacks write-without-response")
                chunks = command_chunks(packet, getattr(characteristic, "max_write_without_response_size", None))
                probe.message_number = args.message_number
                probe.sent_at = time.monotonic()
                probe.record({"type": "command", "hex": packet.hex(), "message_number": args.message_number}, monotonic=probe.sent_at)
                for index, chunk in enumerate(chunks):
                    probe.record({"type": "command_chunk", "hex": chunk.hex(), "index": index})
                    await client.write_gatt_char(characteristic, chunk, response=False)
            await asyncio.sleep(args.duration)
            await client.stop_notify(notify_char)
    print(f"JSONL: {output}")
    if not args.mutate:
        print("Observe-only: notification subscription active, no Spark command sent; "
              "command-to-response latency unavailable without --mutate.")
    else:
        print("One FX command sent; matching 03/15 and 04/15 latencies are in JSONL (if received). No restore.")


if __name__ == "__main__":
    asyncio.run(run(parse_args()))
