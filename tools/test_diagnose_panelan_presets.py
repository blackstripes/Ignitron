"""Host-only diagnostic state-machine tests; never open serial hardware."""
import unittest
from diagnose_panelan_presets import Soak, status


READY = ["--- Ignitron status ---", "Spark connected: yes", "Amp: Spark 2",
         "Serial: S123", "Bank: 0  Preset: 1", "Preset name: Clean", "Looper loops: 0"]


def event(s, kind, **fields):
    s.on_line("PRESET_TRACE t=123 event=" + kind + " " + " ".join(f"{k}={v}" for k, v in fields.items()))


def start(s, current=2):
    lines = [line.replace("Preset: 1", f"Preset: {current}") for line in READY]
    s.on_status(status(lines))
    event(s, "startup_full_query", id=0, target=current, sent=1, msg=10, retry=0)
    event(s, "ready", id=0, target=current, msg=10)
    assert s.send_due(0) == 1


def accepted(s, target=1):
    event(s, "accept", id=7, target=target, confirmed=2)
    event(s, "sent", id=7, target=target)
    event(s, "number_query", id=7, target=target, sent=1)
    event(s, "number_confirm", id=7, target=target, confirmed=target)
    event(s, "full_query", id=7, target=target, sent=1, msg=31)


def parsed(s, msg=31):
    event(s, "multipart_complete", msg=msg, cmd="03", sub="01", expected=17, received=17, reason="none")
    event(s, "preset_parse_complete", msg=msg, cmd="03", sub="01", slot=1, frames=17, bytes=2100)


class DiagnosticTests(unittest.TestCase):
    def test_clean_ready_and_cadence(self):
        s = Soak(count=2)
        start(s)
        accepted(s)
        parsed(s)
        event(s, "preset_apply_gate", msg=31, expected=31, match=1, owner_active=1,
              owner_msg=31, owner_sub="01", slot=1, cache=1, acceptedActive=1,
              revision_before=1, revision_after=2)
        event(s, "preset_observation_publish", msg=31, slot=1, revision=2)
        event(s, "ready", id=7, target=1, msg=31)
        self.assertIsNone(s.stop)
        self.assertEqual(s.last_ready, ("7", "1"))
        self.assertIsNone(s.send_due(2.9))
        self.assertEqual(s.send_due(3), 2)

    def test_noop_advances_without_count(self):
        s = Soak()
        start(s, current=1)
        event(s, "reject", target=1, confirmed=1, reason="already_current")
        self.assertEqual(s.accepted, 0)
        self.assertIsNone(s.send_due(2))
        self.assertEqual(s.send_due(3), 2)

    def test_sent_zero(self):
        s = Soak()
        start(s)
        event(s, "accept", id=7, target=1, confirmed=2)
        event(s, "full_query", id=7, target=1, sent=0, msg=0)
        self.assertIn("sent=0", s.stop)

    def test_complete_parsed_without_publish_not_premature(self):
        s = Soak()
        start(s)
        accepted(s)
        parsed(s)
        event(s, "preset_apply_gate", msg=31, expected=31, match=1, owner_active=0,
              owner_msg=0, owner_sub="00", slot=1, cache=1, acceptedActive=0,
              revision_before=1, revision_after=1)
        event(s, "response_lane_complete", msg=31, cmd="03", sub="01", active_before=0,
              owner_msg=0, owner_sub="00", matched=0, released=0)
        self.assertIn("did not publish", s.stop)
        self.assertEqual(s.parsed["gate"]["acceptedActive"], "0")

    def test_stale_superseded_is_not_anomaly(self):
        s = Soak()
        start(s)
        accepted(s)
        event(s, "startup_full_query", id=7, target=1, sent=1, msg=32, retry=0)
        parsed(s, msg=31)
        event(s, "preset_apply_gate", msg=31, expected=32, match=0, owner_active=0)
        event(s, "multipart_discard", msg=40, cmd="03", sub="01", expected=17, received=16, reason="new_start")
        self.assertIsNone(s.stop)
        self.assertIsNone(s.parsed)

    def test_full_timeout_and_disconnect(self):
        s = Soak()
        start(s)
        accepted(s)
        event(s, "full_data_timeout", id=7, target=1, msg=31)
        self.assertIn("full_data_timeout", s.stop)
        s = Soak()
        start(s)
        event(s, "disconnect")
        self.assertEqual(s.stop, "Spark disconnected")

    def test_connected_status_loss_and_trace_loss_stop(self):
        s = Soak()
        start(s)
        s.on_status(status([*READY[:1], "Spark connected: no", *READY[2:]]))
        self.assertIn("lost in status", s.stop)
        s = Soak()
        start(s)
        event(s, "ingress_trace_lost", count=3)
        self.assertIn("trace evidence lost", s.stop)

    def test_current_incoming_reject_stops(self):
        s = Soak()
        start(s)
        accepted(s)
        event(s, "incoming_reject", msg=31, cmd="03", sub="01", expected=17,
              received=16, reason="malformed")
        self.assertIn("frame rejected", s.stop)

    def test_reset_then_spark2_readiness(self):
        s = Soak()
        s.on_status(status([*READY[:1], "Spark connected: no", "Amp: (unknown)",
                            "Serial: (unknown)", "Preset name: (unknown)", "Looper loops: 0"]))
        self.assertIsNone(s.send_due(0))
        s.tick(20)
        self.assertIsNone(s.stop)
        s.on_status(status(READY))
        self.assertIsNone(s.send_due(21))
        event(s, "startup_full_query", id=0, target=2, sent=1, msg=10)
        event(s, "ready", id=0, target=2, msg=10)
        self.assertIsNone(s.send_due(21))  # status slot must agree with startup Ready
        s.on_status(status([*READY[:4], "Bank: 0  Preset: 2", *READY[5:]]))
        self.assertEqual(s.send_due(21), 1)
        wrong = Soak()
        wrong.on_status(status([x.replace("Spark 2", "Spark NEO") for x in READY]))
        self.assertIn("wrong amp", wrong.stop)

    def test_missing_fields_and_current_discard(self):
        s = Soak()
        start(s)
        accepted(s)
        event(s, "preset_parse_complete", msg=31, cmd="03", sub="01", slot=1, frames=17)
        self.assertIn("missing trace fields bytes", s.stop)
        s = Soak()
        start(s)
        accepted(s)
        event(s, "multipart_discard", msg=31, cmd="03", sub="01", expected=17, received=16, reason="new_start")
        self.assertIn("multipart anomaly", s.stop)

    def test_startup_timeout_no_commands(self):
        s = Soak()
        s.on_status(status(READY))
        event(s, "startup_full_query", id=0, target=2, sent=1, msg=10)
        event(s, "startup_full_timeout", id=0, target=2, msg=10)
        self.assertIn("startup full timeout", s.stop)
        self.assertIsNone(s.send_due(20))

    def test_startup_retry_stops_before_commands(self):
        s = Soak()
        s.on_status(status(READY))
        event(s, "startup_full_query", id=0, target=1, sent=1, msg=10, retry=1)
        self.assertIn("retry", s.stop)
        self.assertIsNone(s.send_due(20))


if __name__ == "__main__":
    unittest.main()
