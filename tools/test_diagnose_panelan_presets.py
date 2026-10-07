"""Host-only diagnostic state-machine tests; never open serial hardware."""
from contextlib import redirect_stderr
from io import StringIO
import unittest
from diagnose_panelan_presets import Soak, parse_args, status, trace


READY = ["--- Ignitron status ---", "Spark connected: yes", "Amp: Spark 2",
         "Serial: S123", "Bank: 0  Preset: 1", "Active hardware slot: 1",
         "Preset name: Clean", "Looper loops: 0"]


def slot_lines(slot):
    return [line.replace("Preset: 1", f"Preset: {(slot - 1) % 4 + 1}").replace(
        "Active hardware slot: 1", f"Active hardware slot: {slot}") for line in READY]


def snapshot_status(slot=2, **overrides):
    fields = {"Controller phase": "Ready", "identityKnown": "true", "sparkStateStale": "false",
              "fullPresetObservedForLink": "true", "confirmedHardwarePreset": str(slot),
              "pendingHardwarePreset": "0", "presetActionFailed": "false"}
    fields.update(overrides)
    return status(slot_lines(slot)[:-1] +
                   [f"{key}: {value}" for key, value in fields.items()] + [READY[-1]])


def event(s, kind, **fields):
    s.on_line("PRESET_TRACE t=123 event=" + kind + " " + " ".join(f"{k}={v}" for k, v in fields.items()))


def start(s, current=2):
    lines = slot_lines(current)
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
    def test_count_500_accepted_without_weakening_other_limits(self):
        self.assertEqual(parse_args(["--port", "dummy", "--count", "500"]).count, 500)
        for options in (["--count", "501"], ["--count", "0"], ["--cadence", "2"],
                        ["--action-timeout", "0"], ["--ready-timeout", "0"]):
            with self.subTest(options=options), redirect_stderr(StringIO()), self.assertRaises(SystemExit):
                parse_args(["--port", "dummy", *options])

    def test_trace_prompt_prefixes_normalize_only_at_line_start(self):
        plain = "PRESET_TRACE t=1 event=accept id=1 target=1 confirmed=3"
        expected = trace(plain)
        self.assertIsNotNone(expected)
        self.assertEqual(expected["event"], "accept")
        self.assertEqual(trace("> PRESET_TRACE t=1 event=accept id=1 target=1 confirmed=3"), expected)
        self.assertEqual(trace(">  PRESET_TRACE t=1 event=accept id=1 target=1 confirmed=3"), expected)
        self.assertEqual(trace("> > PRESET_TRACE t=1 event=accept id=1 target=1 confirmed=3"), expected)
        self.assertIsNone(trace("noise before PRESET_TRACE t=1 event=accept id=1 target=1"))

    def test_prompt_prefixed_status_block_markers_parse(self):
        # Build a complete block from actual line-oriented output; prompt syntax
        # can prefix the block header and final marker as well as fields.
        lines = ["> --- Ignitron status ---", "> Spark connected: yes", "Amp: Spark 2",
                 "Serial: S123", "Bank: 0  Preset: 2", "Active hardware slot: 2",
                 "Preset name: Clean", "Controller phase: Ready", "identityKnown: true",
                 "sparkStateStale: false", "fullPresetObservedForLink: true",
                 "confirmedHardwarePreset: 2", "> > Looper loops: 0"]
        values = status(lines)
        self.assertIsNotNone(values)
        self.assertEqual(values["Controller phase"], "Ready")
        self.assertEqual(values["Current preset"], "2")

    def test_prompt_prefixed_action_one_sequence_correlates(self):
        s = Soak(count=1)
        s.on_status(snapshot_status(slot=3))
        self.assertEqual(s.send_due(0), 1)
        s.on_line("> PRESET_TRACE t=100 event=accept id=1 target=1 confirmed=3")
        s.on_line("PRESET_TRACE t=101 event=queued id=1 target=1 elapsed=0")
        event(s, "sent", id=1, target=1)
        event(s, "number_query", id=1, target=1, sent=1)
        event(s, "number_confirm", id=1, target=1, confirmed=1)
        event(s, "full_query", id=1, target=1, sent=1, msg=12)
        parsed(s, msg=12)
        event(s, "preset_apply_gate", msg=12, expected=12, match=1, owner_active=1,
              owner_msg=12, owner_sub="01", slot=0, cache=0, acceptedActive=1,
              revision_before=1, revision_after=2)
        event(s, "preset_observation_publish", msg=12, slot=0, revision=2)
        event(s, "response_lane_complete", msg=12, cmd="03", sub="01", active_before=1,
              owner_msg=12, owner_sub="01", matched=1, released=1)
        event(s, "ready", id=1, target=1, msg=12)
        self.assertEqual(s.accepted, 1)
        self.assertEqual(s.last_ready, ("1", "1"))
        self.assertEqual(s.stop, "completed")

    def test_full_hardware_slot_snapshot_match_and_within_bank_mismatch(self):
        for slot in (5, 8):
            with self.subTest(slot=slot):
                values = snapshot_status(slot)
                self.assertEqual(values["Current preset"], str(slot))
                self.assertEqual(values["Within-bank preset"], str((slot - 1) % 4 + 1))
                s = Soak()
                s.on_status(values)
                self.assertTrue(s.started)
                self.assertEqual(s.current, str(slot))
                wrong = Soak()
                wrong.on_status(snapshot_status(slot, confirmedHardwarePreset=str((slot - 1) % 4 + 1)))
                self.assertIsNone(wrong.send_due(0))

    def test_full_hardware_slot_trace_match_and_within_bank_mismatch(self):
        for slot in (5, 8):
            with self.subTest(slot=slot):
                s = Soak()
                start(s, current=slot)
                self.assertEqual(s.current, str(slot))
                wrong = Soak()
                wrong.on_status(status(slot_lines(slot)))
                event(wrong, "startup_full_query", id=0, target=(slot - 1) % 4 + 1, sent=1, msg=10)
                event(wrong, "ready", id=0, target=(slot - 1) % 4 + 1, msg=10)
                self.assertIsNone(wrong.send_due(0))

    def test_legacy_bank_is_not_authoritative(self):
        lines = [line for line in slot_lines(5) if not line.startswith("Active hardware slot:")]
        # Even a UI Bank: 1 does not establish hardware slot 5.
        lines = [line.replace("Bank: 0", "Bank: 1") for line in lines]
        s = Soak()
        s.on_status(status(lines[:-1] + ["Controller phase: Ready", "identityKnown: true",
                                     "sparkStateStale: false", "fullPresetObservedForLink: true",
                                     "confirmedHardwarePreset: 5", lines[-1]]))
        self.assertIsNone(s.send_due(0))
        event(s, "startup_full_query", id=0, target=5, sent=1, msg=10)
        event(s, "ready", id=0, target=5, msg=10)
        self.assertIsNone(s.send_due(0))
        legacy = Soak()
        legacy.on_status(status(lines[:-1] + ["Active hardware bank: 1", lines[-1]]))
        event(legacy, "startup_full_query", id=0, target=5, sent=1, msg=10)
        event(legacy, "ready", id=0, target=5, msg=10)
        self.assertEqual(legacy.send_due(0), 1)

    def test_explicit_slot_takes_precedence_over_bank(self):
        s = Soak()
        lines = slot_lines(8)
        s.on_status(status(lines[:-1] + ["Active hardware bank: 0", lines[-1]]))
        event(s, "startup_full_query", id=0, target=4, sent=1, msg=10)
        event(s, "ready", id=0, target=4, msg=10)
        self.assertIsNone(s.send_due(0))

    def test_already_ready_before_attach_status_alone(self):
        s = Soak()
        s.on_status(snapshot_status())
        self.assertTrue(s.started)
        self.assertEqual(s.current, "2")
        self.assertEqual(s.send_due(0), 1)

    def test_syncing_cached_data_cannot_start_even_with_trace(self):
        s = Soak()
        s.on_status(snapshot_status(**{"Controller phase": "Syncing"}))
        event(s, "startup_full_query", id=0, target=2, sent=1, msg=10)
        event(s, "ready", id=0, target=2, msg=10)
        self.assertIsNone(s.send_due(0))

    def test_full_observation_false_cannot_start(self):
        s = Soak()
        s.on_status(snapshot_status(fullPresetObservedForLink="false"))
        self.assertIsNone(s.send_due(0))

    def test_stale_cannot_start(self):
        s = Soak()
        s.on_status(snapshot_status(sparkStateStale="true"))
        self.assertIsNone(s.send_due(0))

    def test_confirmed_mismatch_cannot_start(self):
        s = Soak()
        s.on_status(snapshot_status(confirmedHardwarePreset="3"))
        self.assertIsNone(s.send_due(0))

    def test_status_becomes_ready_after_reset(self):
        s = Soak()
        s.on_status(snapshot_status(**{"Spark connected": "no", "Controller phase": "Scanning"}))
        self.assertIsNone(s.send_due(0))
        s.on_status(snapshot_status(**{"Controller phase": "Syncing", "fullPresetObservedForLink": "false"}))
        self.assertIsNone(s.send_due(1))
        s.on_status(snapshot_status())
        self.assertEqual(s.send_due(2), 1)

    def test_startup_sent_zero_reports_transport_status(self):
        s = Soak()
        lines = slot_lines(2)[:-1]
        lines += ["lastSubmissionStatus: Sent", "currentCommand hasRemaining: false  remainingParts: 0",
                  "responseLane active: false  owner msg: 0  owner sub: 00",
                  "controllerFullPresetMessageNumber: 0", READY[-1]]
        s.on_status(status(lines))
        event(s, "startup_full_query", id=0, target=2, sent=0, msg=0,
              lastSubmissionStatus="Failed", currentCommandHasRemaining=1, currentCommandRemainingParts=3,
              responseLaneActive=1, ownerMsg=12, ownerSub="01", controllerFullPresetMessageNumber=12)
        self.assertIn("event transport=", s.stop)
        self.assertIn("'lastSubmissionStatus': 'Failed'", s.stop)
        self.assertIn("'currentCommandRemainingParts': '3'", s.stop)
        self.assertIn("'ownerMsg': '12'", s.stop)
        self.assertIn("'ownerSub': '01'", s.stop)
        self.assertIn("latest status transport={'lastSubmissionStatus': 'Sent'", s.stop)
        self.assertIsNone(s.send_due(0))

    def test_startup_busy_waits_for_sync_retry(self):
        s = Soak()
        s.on_status(status(slot_lines(2)))
        event(s, "startup_full_query", id=0, target=2, sent=0, msg=0,
              lastSubmissionStatus="Busy", currentCommandHasRemaining=1,
              currentCommandRemainingParts=2, responseLaneActive=1, ownerMsg=11, ownerSub="10",
              controllerFullPresetMessageNumber=0)
        self.assertIsNone(s.stop)
        event(s, "startup_full_query", id=0, target=2, sent=1, msg=12, retry=0)
        event(s, "ready", id=0, target=2, msg=12)
        self.assertEqual(s.send_due(0), 1)

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
        event(s, "full_query", id=7, target=1, sent=0, msg=0,
              lastSubmissionStatus="Busy", currentCommandHasRemaining=1, currentCommandRemainingParts=2,
              responseLaneActive=1, ownerMsg=31, ownerSub="01", controllerFullPresetMessageNumber=31)
        self.assertIn("sent=0", s.stop)
        self.assertIn("'lastSubmissionStatus': 'Busy'", s.stop)
        self.assertIn("'currentCommandRemainingParts': '2'", s.stop)
        self.assertIn("'ownerMsg': '31'", s.stop)

    def test_busy_number_query_retries_then_ready_and_counts_sent_polls(self):
        s = Soak(count=1)
        start(s)
        event(s, "accept", id=7, target=1, confirmed=2)
        for _ in range(2):
            event(s, "number_query", id=7, target=1, sent=0, lastSubmissionStatus="Busy")
            self.assertIsNone(s.stop)
        for _ in range(2):
            event(s, "number_query", id=7, target=1, sent=1, lastSubmissionStatus="Sent")
        event(s, "number_confirm", id=7, target=1, confirmed=1)
        event(s, "full_query", id=7, target=1, sent=1, msg=31)
        parsed(s)
        event(s, "preset_observation_publish", msg=31, slot=1, revision=2)
        event(s, "ready", id=7, target=1, msg=31)
        self.assertEqual(s.stop, "completed")
        self.assertEqual(s.number_query_attempts, 4)
        self.assertEqual(s.number_query_sent_zero["Busy"], 2)
        self.assertEqual(s.number_query_by_action, {"7": {"attempts": 4, "sent": 2}})

    def test_non_busy_or_missing_number_query_status_is_terminal(self):
        for submission in ("Failed", "Sent", None):
            with self.subTest(submission=submission):
                s = Soak()
                start(s)
                event(s, "accept", id=7, target=1, confirmed=2)
                fields = {} if submission is None else {"lastSubmissionStatus": submission}
                event(s, "number_query", id=7, target=1, sent=0, **fields)
                self.assertIn("number_query sent=0", s.stop)
                self.assertEqual(s.number_query_sent_zero[submission or "(missing)"], 1)
                self.assertEqual(s.number_query_attempts, 1)

    def test_number_timeout_remains_terminal_after_busy_poll(self):
        s = Soak()
        start(s)
        event(s, "accept", id=7, target=1, confirmed=2)
        event(s, "number_query", id=7, target=1, sent=0, lastSubmissionStatus="Busy")
        event(s, "fail", id=7, target=1, reason="number_timeout")
        self.assertIn("number_timeout", s.stop)
        self.assertEqual(s.number_query_by_action["7"], {"attempts": 1, "sent": 0})

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
        s.on_status(status(slot_lines(2)))
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
