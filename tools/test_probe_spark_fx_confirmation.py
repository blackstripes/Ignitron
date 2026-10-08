"""Offline-only Spark FX probe checks; no Bleak import or hardware access."""

import asyncio
import io
import json
import sys
import tempfile
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import probe_spark_fx_confirmation as probe


def inbound(number, command, payload):
    encoded = probe.encode_7bit(payload)
    return bytes((0xf0, 1, number, probe.checksum(encoded), command, 0x15)) + encoded + b"\xf7"


def block(frame):
    return probe.BLOCK_START + bytes((16 + len(frame),)) + bytes(9) + frame


class SparkProbeTests(unittest.TestCase):
    def test_args(self):
        args = probe.parse_args(["--address", "AA:BB:CC:DD:EE:FF"])
        self.assertFalse(args.mutate)
        self.assertIsNone(args.effect)
        self.assertEqual(args.duration, 15)
        for argv in ([], ["--address", ""], ["--address", "a", "--mutate"],
                     ["--address", "a", "--mutate", "--effect", "Drive", "--state", "on"],
                     ["--address", "a", "--effect", "Drive"],
                     ["--address", "a", "--duration", "nan"],
                     ["--address", "a", "--mutate", "--effect", "Drive", "--state", "off",
                      "--message-number", "0"]):
            with self.subTest(argv=argv), self.assertRaises(SystemExit):
                probe.parse_args(argv)
        args = probe.parse_args(["--address", "a", "--mutate", "--effect", "Drive",
                                 "--state", "off", "--message-number", "255", "--duration", "2"])
        self.assertTrue(args.mutate)
        self.assertEqual(args.message_number, 255)

    def test_exact_command_and_checksum(self):
        # Drive payload: 05 A5 44 72 69 76 65 C3 00; 7-bit masks 02, 01.
        packet = probe.build_effect_command("Drive", "on", 0x2a)
        expected_frame = bytes.fromhex("f0 01 2a 2c 01 15 02 05 25 44 72 69 76 65 01 43 00 f7")
        self.assertEqual(packet, bytes.fromhex("01 fe 00 00 53 fe 22") + bytes(9) + expected_frame)
        self.assertEqual(probe.decode_7bit(expected_frame[6:-1]), bytes.fromhex("05 a5 44 72 69 76 65 c3 00"))
        self.assertEqual(probe.parse_frame(expected_frame)["checksum_ok"], True)
        self.assertEqual(probe.build_effect_command("Drive", "off", 0x2a)[-3], 0x42)
        damaged = bytearray(expected_frame)
        damaged[3] ^= 1
        self.assertEqual(probe.parse_frame(damaged)["error"], "checksum mismatch")

    def test_transport_chunks(self):
        packet = probe.build_effect_command("Drive", "on", 42)
        self.assertEqual(len(packet), 34)
        chunks = probe.command_chunks(packet, 20)
        self.assertEqual([len(chunk) for chunk in chunks], [20, 14])
        self.assertEqual(b"".join(chunks), packet)
        for limit in (None, 0, -1, True, "20"):
            with self.subTest(limit=limit), self.assertRaises(ValueError):
                probe.command_chunks(packet, limit)

    def test_mutation_uses_discovered_limit_or_refuses_write(self):
        class FakeClient:
            def __init__(self, _address):
                self.characteristic = SimpleNamespace(max_write_without_response_size=20,
                                                      properties=["write-without-response"])
                self.notify_characteristic = SimpleNamespace(properties=["notify"])
                self.service = SimpleNamespace(
                    get_characteristic=lambda uuid: self.notify_characteristic if uuid == probe.NOTIFY_UUID
                    else self.characteristic if uuid == probe.WRITE_UUID else None)
                self.services = SimpleNamespace(get_service=lambda uuid: self.service if uuid == probe.SERVICE_UUID else None)
                self.writes = []

            async def __aenter__(self):
                return self

            async def __aexit__(self, *_args):
                pass

            async def start_notify(self, *_args):
                pass

            async def stop_notify(self, *_args):
                pass

            async def write_gatt_char(self, characteristic, data, *, response):
                self.writes.append((characteristic, data, response))

        with tempfile.TemporaryDirectory() as directory:
            args = probe.parse_args(["--address", "fake", "--mutate", "--effect", "Drive",
                                     "--state", "on", "--message-number", "42", "--duration", "0.01"])
            instances = []

            def client_factory(address):
                client = FakeClient(address)
                instances.append(client)
                return client

            with patch.dict(sys.modules, {"bleak": SimpleNamespace(BleakClient=client_factory)}):
                args.output = Path(directory) / "valid.jsonl"
                asyncio.run(probe.run(args))
                client = instances[-1]
                self.assertEqual([len(data) for _, data, _ in client.writes], [20, 14])
                self.assertEqual(b"".join(data for _, data, _ in client.writes),
                                 probe.build_effect_command("Drive", "on", 42))
                self.assertTrue(all(char is client.characteristic and not response
                                    for char, _, response in client.writes))
                rows = [json.loads(line) for line in args.output.read_text().splitlines()]
                self.assertEqual([row["type"] for row in rows],
                                 ["command", "command_chunk", "command_chunk"])
                self.assertEqual("".join(row["hex"] for row in rows[1:]), rows[0]["hex"])

                args.output = Path(directory) / "invalid.jsonl"
                instances.clear()

                def missing_limit(address):
                    client = FakeClient(address)
                    client.characteristic = SimpleNamespace(properties=["write-without-response"])
                    instances.append(client)
                    return client

                with patch.dict(sys.modules, {"bleak": SimpleNamespace(BleakClient=missing_limit)}):
                    with self.assertRaisesRegex(ValueError, "refusing mutation"):
                        asyncio.run(probe.run(args))
                self.assertEqual(instances[-1].writes, [])

    def test_observe_only_subscribes_but_sends_no_protocol_write(self):
        class FakeClient:
            def __init__(self, _address):
                self.notify_characteristic = SimpleNamespace(properties=["notify"])
                self.characteristic = SimpleNamespace(properties=["write-without-response"],
                                                      max_write_without_response_size=20)
                self.service = SimpleNamespace(
                    get_characteristic=lambda uuid: self.notify_characteristic if uuid == probe.NOTIFY_UUID
                    else self.characteristic if uuid == probe.WRITE_UUID else None)
                self.services = SimpleNamespace(get_service=lambda uuid: self.service if uuid == probe.SERVICE_UUID else None)
                self.writes = []
                self.subscribed = False

            async def __aenter__(self):
                return self

            async def __aexit__(self, *_args):
                pass

            async def start_notify(self, _char, _callback):
                self.subscribed = True

            async def stop_notify(self, _char):
                self.subscribed = False

            async def write_gatt_char(self, characteristic, data, *, response):
                self.writes.append((characteristic, data, response))

        with tempfile.TemporaryDirectory() as directory:
            args = probe.parse_args(["--address", "fake", "--duration", "0.01"])
            clients = []

            def client_factory(address):
                client = FakeClient(address)
                clients.append(client)
                return client

            args.output = Path(directory) / "observe.jsonl"
            with patch.dict(sys.modules, {"bleak": SimpleNamespace(BleakClient=client_factory)}):
                asyncio.run(probe.run(args))
            self.assertFalse(clients[0].subscribed)
            self.assertEqual(clients[0].writes, [])

    def test_split_coalesced_and_malformed_preserved(self):
        state = inbound(42, 3, bytes.fromhex("05 a5 44 72 69 76 65 c3 00"))
        ack = inbound(42, 4, b"")
        parser = probe.SparkParser()
        wrapped = block(state)
        self.assertEqual(parser.feed(wrapped[:3]), [])
        self.assertEqual(parser.feed(wrapped[3:18]), [])
        events = parser.feed(wrapped[18:] + ack + state)
        self.assertEqual([item["type"] for item in events], ["block", "frame", "frame", "frame"])
        self.assertEqual([item["event"] for item in events[1:]], ["effect_state", "generic_ack", "effect_state"])
        self.assertEqual(parser.feed(state[:8]), [])
        self.assertEqual(parser.feed(state[8:])[0]["hex"], state.hex())
        bad = bytearray(state)
        bad[3] ^= 1
        self.assertEqual(parser.feed(bad)[0]["hex"], bad.hex())
        self.assertEqual(parser.feed(b"\x99")[0]["type"], "unparsed")
        malformed = probe.parse_frame(inbound(42, 3, b"\x05\xa5Dri\x01"))
        self.assertIn("error", malformed)
        self.assertEqual(malformed["payload_hex"], "05a544726901")

    def test_frame_continues_across_wrappers_and_split_prefix(self):
        state = inbound(42, 3, bytes.fromhex("05 a5 44 72 69 76 65 c3 00"))
        ack = inbound(42, 4, b"")
        first, second = block(state[:9]), block(state[9:])
        parser = probe.SparkParser()
        self.assertEqual(parser.feed(first + second[:1]), [{"type": "block", "hex": first.hex(), "size": len(first)}])
        self.assertEqual(parser.feed(second[1:2]), [])
        events = parser.feed(second[2:] + block(ack))
        self.assertEqual([event["type"] for event in events], ["block", "frame", "block", "frame"])
        self.assertEqual([event["hex"] for event in events],
                         [second.hex(), state.hex(), block(ack).hex(), ack.hex()])
        self.assertEqual([event["event"] for event in events if event["type"] == "frame"],
                         ["effect_state", "generic_ack"])

    def test_faked_notifications_latency(self):
        sink = io.StringIO()
        recorder = probe.Probe(sink)
        recorder.message_number = 42
        recorder.sent_at = 10.0
        effect = inbound(42, 3, bytes.fromhex("05 a5 44 72 69 76 65 c2 00"))
        ack = inbound(42, 4, b"")
        recorder.notification(None, effect[:5], wall="test", monotonic=10.1)
        recorder.notification(None, effect[5:] + ack + inbound(43, 4, b""), wall="test", monotonic=10.25)
        rows = [json.loads(line) for line in sink.getvalue().splitlines()]
        self.assertEqual([row["hex"] for row in rows if row["type"] == "notification"],
                         [effect[:5].hex(), (effect[5:] + ack + inbound(43, 4, b"")).hex()])
        frames = [row for row in rows if row["type"] == "frame"]
        self.assertEqual((frames[0]["effect"], frames[0]["state"]), ("Drive", "off"))
        self.assertEqual([row["event"] for row in frames], ["effect_state", "generic_ack", "generic_ack"])
        self.assertEqual([row.get("latency_ms") for row in frames], [250.0, 250.0, None])
        self.assertEqual(frames[0]["wall"], "test")
        self.assertNotIn("bleak", sys.modules)


if __name__ == "__main__":
    unittest.main()
