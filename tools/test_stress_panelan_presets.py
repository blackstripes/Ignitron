import unittest

from stress_panelan_presets import final_confirmed_target, record_trace, status_is_ready


class TraceTest(unittest.TestCase):
    def test_ready_requires_link_identity_and_preset_data(self):
        self.assertFalse(status_is_ready(["Spark connected: yes", "Preset name: CLEAN"]))
        self.assertFalse(status_is_ready([
            "Spark connected: yes", "Amp: Spark NEO Core", "Serial: SHP1", "Preset name: (unknown)"]))
        self.assertTrue(status_is_ready([
            "Spark connected: yes", "Amp: Spark NEO Core", "Serial: SHP1", "Preset name: CLEAN"]))

    def parse(self, lines):
        records, outcomes = {}, []
        for line in lines:
            record_trace(line, records, outcomes)
        return records, outcomes

    def test_last_command_not_earlier_same_target(self):
        records, outcomes = self.parse([
            "PRESET_TRACE event=accept id=1 target=2 t=1",
            "PRESET_TRACE event=number_confirm id=1 target=2 t=3",
            "PRESET_TRACE event=accept id=2 target=3 t=4",
            "PRESET_TRACE event=number_confirm id=2 target=3 t=6",
            "PRESET_TRACE event=accept id=3 target=2 t=7",
            "PRESET_TRACE event=sent id=3 target=2 t=8",
        ])
        self.assertIsNone(final_confirmed_target([2, 3, 2], records, outcomes))
        record_trace("PRESET_TRACE event=number_confirm id=3 target=2 t=9", records, outcomes)
        self.assertEqual(final_confirmed_target([2, 3, 2], records, outcomes), 2)

    def test_deferred_target_already_observed_needs_no_send(self):
        records, outcomes = self.parse([
            "PRESET_TRACE event=accept id=1 target=2 t=1",
            "PRESET_TRACE event=satisfied id=1 target=2 confirmed=2 elapsed=2",
        ])
        self.assertEqual(final_confirmed_target([2], records, outcomes), 2)

    def test_queued_event_recovers_dropped_accept_trace(self):
        records, outcomes = self.parse([
            "PRESET_TRACE event=queued id=7 target=3 elapsed=0",
            "PRESET_TRACE event=satisfied id=7 target=3 confirmed=3 elapsed=4",
        ])
        self.assertEqual(len(outcomes), 1)
        self.assertEqual(final_confirmed_target([3], records, outcomes), 3)

    def test_confirmed_then_overridden(self):
        records, outcomes = self.parse([
            "PRESET_TRACE event=accept id=1 target=2",
            "PRESET_TRACE event=number id=1 confirmed=2",
            "PRESET_TRACE event=number_confirm id=1 target=2 confirmed=2",
            "PRESET_TRACE event=number id=1 confirmed=3",
        ])
        self.assertIsNone(final_confirmed_target([2], records, outcomes))

    def test_final_number_wins_and_noops(self):
        for outcome in ("PRESET_TRACE event=satisfied id=1 target=2 confirmed=2",
                        "PRESET_TRACE event=number_confirm id=1 target=2 confirmed=2"):
            records, outcomes = self.parse([
                "PRESET_TRACE event=accept id=1 target=2", outcome,
                "PRESET_TRACE event=number confirmed=3",
                "PRESET_TRACE event=number confirmed=2",
            ])
            self.assertEqual(final_confirmed_target([2], records, outcomes), 2)
        records, outcomes = self.parse([
            "PRESET_TRACE event=reject target=2 reason=already_current confirmed=2",
            "PRESET_TRACE event=number confirmed=3",
        ])
        self.assertIsNone(final_confirmed_target([2], records, outcomes))
        record_trace("PRESET_TRACE event=number confirmed=2", records, outcomes)
        self.assertEqual(final_confirmed_target([2], records, outcomes), 2)

    def test_reject_fail_and_missing_outcome(self):
        prefix = ["PRESET_TRACE event=accept id=1 target=2 t=1",
                  "PRESET_TRACE event=number_confirm id=1 target=2 t=2"]
        for suffix in (["PRESET_TRACE event=reject target=2 reason=phase"],
                       ["PRESET_TRACE event=accept id=2 target=2 t=3",
                        "PRESET_TRACE event=fail id=2 target=2 reason=number_timeout"]):
            records, outcomes = self.parse(prefix + suffix)
            self.assertIsNone(final_confirmed_target([2, 2], records, outcomes))
        records, outcomes = self.parse(prefix)
        self.assertIsNone(final_confirmed_target([2, 2], records, outcomes))


if __name__ == "__main__":
    unittest.main()
