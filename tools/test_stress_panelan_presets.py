import unittest

from stress_panelan_presets import final_confirmed_target, final_synced_target, record_trace, status_is_ready


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

    def test_number_confirmation_then_full_timeout_is_not_synced(self):
        records, outcomes = self.parse([
            "PRESET_TRACE event=accept id=6 target=2",
            "PRESET_TRACE event=number_confirm id=6 target=2 confirmed=2",
            "PRESET_TRACE event=full_query id=6 target=2 sent=1 msg=16",
            "PRESET_TRACE event=full_data_timeout id=6 target=2 msg=16",
        ])
        self.assertEqual(final_confirmed_target([2], records, outcomes), 2)
        self.assertIsNone(final_synced_target([2], records, outcomes))
        record_trace("PRESET_TRACE event=full_refresh id=6 target=2 msg=17", records, outcomes)
        self.assertIsNone(final_synced_target([2], records, outcomes))

    def test_matching_full_refresh_passes_only_after_refresh(self):
        records, outcomes = self.parse([
            "PRESET_TRACE event=accept id=6 target=2",
            "PRESET_TRACE event=number_confirm id=6 target=2 confirmed=2",
            "PRESET_TRACE event=full_query id=6 target=2 sent=1 msg=16",
        ])
        self.assertIsNone(final_synced_target([2], records, outcomes))
        record_trace("PRESET_TRACE event=full_refresh id=6 target=2 msg=16", records, outcomes)
        self.assertEqual(final_synced_target([2], records, outcomes), 2)
        record_trace("PRESET_TRACE event=full_data_conflict id=6 target=2 msg=16", records, outcomes)
        self.assertIsNone(final_synced_target([2], records, outcomes))

    def test_deferred_satisfied_requires_matching_ready_startup_result(self):
        records, outcomes = self.parse([
            "PRESET_TRACE event=accept id=1 target=2",
            "PRESET_TRACE event=satisfied id=1 target=2 confirmed=2",
            "PRESET_TRACE event=startup_full_query id=1 target=2 msg=24 sent=1 match=0 ready=0 retry=0",
            "PRESET_TRACE event=startup_full_result id=1 target=2 msg=23 sent=1 match=0 ready=0",
        ])
        self.assertIsNone(final_synced_target([2], records, outcomes))
        record_trace("PRESET_TRACE event=startup_full_result id=1 target=2 msg=24 sent=1 match=1 ready=0", records, outcomes)
        self.assertIsNone(final_synced_target([2], records, outcomes))
        record_trace("PRESET_TRACE event=startup_full_result id=1 target=2 msg=24 sent=1 match=1 ready=1", records, outcomes)
        self.assertEqual(final_synced_target([2], records, outcomes), 2)

    def test_noop_requires_full_readiness(self):
        records, outcomes = self.parse([
            "PRESET_TRACE event=startup_full_query id=0 target=2 msg=12 sent=1 match=0 ready=0 retry=0",
            "PRESET_TRACE event=startup_full_result id=0 target=2 msg=12 sent=1 match=1 ready=1",
            "PRESET_TRACE event=reject target=2 reason=already_current confirmed=2",
        ])
        self.assertEqual(final_synced_target([2], records, outcomes), 2)
        record_trace("PRESET_TRACE event=startup_full_query id=0 target=2 msg=13 sent=1 match=0 ready=0 retry=1", records, outcomes)
        record_trace("PRESET_TRACE event=startup_full_timeout id=0 target=2 msg=13 sent=1 match=0 ready=0", records, outcomes)
        self.assertIsNone(final_synced_target([2], records, outcomes))

    def test_timeout_then_retry_can_sync(self):
        for outcome, confirmation, query, timeout, result in (
            ("accept id=6 target=2", "number_confirm", "full_query", "full_data_timeout", "full_refresh"),
            ("accept id=6 target=2", "satisfied", "startup_full_query", "startup_full_timeout", "startup_full_result"),
            ("reject target=2 reason=already_current confirmed=2", None, "startup_full_query", "startup_full_timeout", "startup_full_result"),
        ):
            with self.subTest(outcome=outcome, query=query):
                lines = [f"PRESET_TRACE event={outcome}"]
                if confirmation:
                    lines.append(f"PRESET_TRACE event={confirmation} id=6 target=2 confirmed=2")
                query_id = "0" if outcome.startswith("reject") else "6"
                lines.extend([
                    f"PRESET_TRACE event={query} id={query_id} target=2 sent=1 msg=16",
                    f"PRESET_TRACE event={timeout} id={query_id} target=2 msg=16",
                    f"PRESET_TRACE event={query} id={query_id} target=2 sent=1 msg=17",
                    f"PRESET_TRACE event={result} id={query_id} target=2 msg=16 sent=1 match=1 ready=1",
                ])
                records, outcomes = self.parse(lines)
                self.assertIsNone(final_synced_target([2], records, outcomes))
                record_trace(f"PRESET_TRACE event={result} id={query_id} target=2 msg=17 sent=1 match=1 ready=1", records, outcomes)
                self.assertEqual(final_synced_target([2], records, outcomes), 2)

    def test_startup_reconnect_revokes_successful_full_refresh(self):
        records, outcomes = self.parse([
            "PRESET_TRACE event=accept id=6 target=2",
            "PRESET_TRACE event=number_confirm id=6 target=2 confirmed=2",
            "PRESET_TRACE event=full_query id=6 target=2 sent=1 msg=16",
            "PRESET_TRACE event=full_refresh id=6 target=2 msg=16",
        ])
        self.assertEqual(final_synced_target([2], records, outcomes), 2)
        record_trace("PRESET_TRACE event=startup_full_query id=0 target=2 sent=1 msg=17 retry=0", records, outcomes)
        self.assertIsNone(final_synced_target([2], records, outcomes))

    def test_disconnect_revokes_full_refresh_until_current_link_result(self):
        records, outcomes = self.parse([
            "PRESET_TRACE event=accept id=6 target=2",
            "PRESET_TRACE event=number_confirm id=6 target=2 confirmed=2",
            "PRESET_TRACE event=full_query id=6 target=2 sent=1 msg=16",
            "PRESET_TRACE event=full_refresh id=6 target=2 msg=16",
        ])
        self.assertEqual(final_synced_target([2], records, outcomes), 2)
        record_trace("PRESET_TRACE event=disconnect", records, outcomes)
        self.assertIsNone(final_synced_target([2], records, outcomes))
        record_trace("PRESET_TRACE event=full_refresh id=6 target=2 msg=16", records, outcomes)
        self.assertIsNone(final_synced_target([2], records, outcomes))
        query = "PRESET_TRACE event=startup_full_query id=0 target=2 sent=1 msg=17"
        result = "PRESET_TRACE event=startup_full_result id=0 target=2 msg=17 sent=1 match=1 ready=1"
        record_trace(query, records, outcomes)
        self.assertIsNone(final_synced_target([2], records, outcomes))
        for invalid in (result.replace("msg=17", "msg=16"),
                        result.replace("match=1", "match=0"),
                        result.replace("ready=1", "ready=0")):
            record_trace(invalid, records, outcomes)
            self.assertIsNone(final_synced_target([2], records, outcomes))
        record_trace(result, records, outcomes)
        self.assertEqual(final_synced_target([2], records, outcomes), 2)

    def test_disconnect_revokes_pending_query_and_noop_readiness(self):
        for prefix in (
            ["PRESET_TRACE event=accept id=6 target=2",
             "PRESET_TRACE event=satisfied id=6 target=2 confirmed=2"],
            ["PRESET_TRACE event=reject target=2 reason=already_current confirmed=2"],
        ):
            with self.subTest(prefix=prefix):
                records, outcomes = self.parse(prefix + [
                    "PRESET_TRACE event=startup_full_query id=0 target=2 sent=1 msg=16",
                    "PRESET_TRACE event=disconnect",
                    "PRESET_TRACE event=startup_full_result id=0 target=2 msg=16 sent=1 match=1 ready=1",
                ])
                self.assertIsNone(final_synced_target([2], records, outcomes))
                record_trace("PRESET_TRACE event=startup_full_query id=0 target=2 sent=1 msg=17", records, outcomes)
                self.assertIsNone(final_synced_target([2], records, outcomes))
                record_trace("PRESET_TRACE event=startup_full_result id=0 target=2 msg=17 sent=1 match=1 ready=1", records, outcomes)
                self.assertEqual(final_synced_target([2], records, outcomes), 2)

    def test_disconnect_revokes_noop_readiness_established_before_rejection(self):
        records, outcomes = self.parse([
            "PRESET_TRACE event=startup_full_query id=0 target=2 sent=1 msg=16",
            "PRESET_TRACE event=startup_full_result id=0 target=2 msg=16 sent=1 match=1 ready=1",
            "PRESET_TRACE event=reject target=2 reason=already_current confirmed=2",
        ])
        self.assertEqual(final_synced_target([2], records, outcomes), 2)
        record_trace("PRESET_TRACE event=disconnect", records, outcomes)
        self.assertIsNone(final_synced_target([2], records, outcomes))

    def test_reconnect_query_for_other_slot_revokes_readiness(self):
        records, outcomes = self.parse([
            "PRESET_TRACE event=accept id=6 target=2",
            "PRESET_TRACE event=number_confirm id=6 target=2 confirmed=2",
            "PRESET_TRACE event=full_query id=6 target=2 sent=1 msg=16",
            "PRESET_TRACE event=full_refresh id=6 target=2 msg=16",
        ])
        self.assertEqual(final_synced_target([2], records, outcomes), 2)
        record_trace("PRESET_TRACE event=startup_full_query id=0 target=3 sent=1 msg=17", records, outcomes)
        self.assertIsNone(final_synced_target([2], records, outcomes))
        # A result claiming target 2 cannot borrow the other slot's query.
        record_trace("PRESET_TRACE event=startup_full_result id=0 target=2 msg=17 sent=1 match=1 ready=1", records, outcomes)
        self.assertIsNone(final_synced_target([2], records, outcomes))
        record_trace("PRESET_TRACE event=startup_full_query id=0 target=2 sent=1 msg=18", records, outcomes)
        record_trace("PRESET_TRACE event=startup_full_result id=0 target=2 msg=18 sent=1 match=1 ready=1", records, outcomes)
        self.assertEqual(final_synced_target([2], records, outcomes), 2)

    def test_number_away_and_back_requires_fresh_full_refresh(self):
        records, outcomes = self.parse([
            "PRESET_TRACE event=accept id=6 target=2",
            "PRESET_TRACE event=number_confirm id=6 target=2 confirmed=2",
            "PRESET_TRACE event=full_query id=6 target=2 sent=1 msg=16",
            "PRESET_TRACE event=full_refresh id=6 target=2 msg=16",
        ])
        self.assertEqual(final_synced_target([2], records, outcomes), 2)
        record_trace("PRESET_TRACE event=number confirmed=3", records, outcomes)
        record_trace("PRESET_TRACE event=number confirmed=2", records, outcomes)
        self.assertEqual(final_confirmed_target([2], records, outcomes), 2)
        self.assertIsNone(final_synced_target([2], records, outcomes))
        # Even a late result for the old query cannot restore readiness.
        record_trace("PRESET_TRACE event=full_refresh id=6 target=2 msg=16", records, outcomes)
        self.assertIsNone(final_synced_target([2], records, outcomes))
        record_trace("PRESET_TRACE event=full_query id=6 target=2 sent=1 msg=17", records, outcomes)
        record_trace("PRESET_TRACE event=full_refresh id=6 target=2 msg=17", records, outcomes)
        self.assertEqual(final_synced_target([2], records, outcomes), 2)

    def test_number_away_clears_pending_reconnect_result(self):
        records, outcomes = self.parse([
            "PRESET_TRACE event=reject target=2 reason=already_current confirmed=2",
            "PRESET_TRACE event=startup_full_query id=0 target=2 sent=1 msg=16",
            "PRESET_TRACE event=number confirmed=3",
            "PRESET_TRACE event=number confirmed=2",
            "PRESET_TRACE event=startup_full_result id=0 target=2 msg=16 sent=1 match=1 ready=1",
        ])
        self.assertIsNone(final_synced_target([2], records, outcomes))
        record_trace("PRESET_TRACE event=startup_full_query id=0 target=2 sent=1 msg=17", records, outcomes)
        record_trace("PRESET_TRACE event=startup_full_result id=0 target=2 msg=17 sent=1 match=1 ready=1", records, outcomes)
        self.assertEqual(final_synced_target([2], records, outcomes), 2)

    def test_reconnect_result_restores_final_refresh_only_for_latest_matching_query(self):
        prefix = [
            "PRESET_TRACE event=accept id=6 target=2",
            "PRESET_TRACE event=number_confirm id=6 target=2 confirmed=2",
            "PRESET_TRACE event=full_query id=6 target=2 sent=1 msg=16",
            "PRESET_TRACE event=full_refresh id=6 target=2 msg=16",
        ]
        query = "PRESET_TRACE event=startup_full_query id=0 target=2 sent=1 msg=17"
        result = "PRESET_TRACE event=startup_full_result id=0 target=2 msg=17 sent=1 match=1 ready=1"
        records, outcomes = self.parse(prefix + [query])
        self.assertIsNone(final_synced_target([2], records, outcomes))
        record_trace(result, records, outcomes)
        self.assertEqual(final_synced_target([2], records, outcomes), 2)

        for suffix in (
            [result],  # A result alone cannot reuse the accepted request's query.
            ["PRESET_TRACE event=startup_full_query id=6 target=2 sent=1 msg=17", result],
            ["PRESET_TRACE event=startup_full_query id=0 target=3 sent=1 msg=17", result],
            [query, result.replace("msg=17", "msg=18")],
            [query.replace("sent=1", "sent=0"), result],
            [query, result.replace("sent=1", "sent=0")],
            [query, result.replace("match=1", "match=0")],
            [query, result.replace("ready=1", "ready=0")],
            [query, query.replace("msg=17", "msg=18"), result],
            [query, "PRESET_TRACE event=startup_full_query id=6 target=2 sent=1 msg=17", result],
            [query, "PRESET_TRACE event=startup_full_timeout id=0 target=2 msg=17", result],
        ):
            with self.subTest(suffix=suffix):
                records, outcomes = self.parse(prefix[:2] + suffix)
                self.assertIsNone(final_synced_target([2], records, outcomes))
        # Even with a prior refresh, a stale result cannot undo a newer query.
        records, outcomes = self.parse(prefix + [query, query.replace("msg=17", "msg=18"), result])
        self.assertIsNone(final_synced_target([2], records, outcomes))
        record_trace(result.replace("msg=17", "msg=18"), records, outcomes)
        self.assertEqual(final_synced_target([2], records, outcomes), 2)

    def test_noop_unpaired_startup_result_cannot_sync(self):
        records, outcomes = self.parse([
            "PRESET_TRACE event=startup_full_result id=0 target=2 msg=12 sent=1 match=1 ready=1",
            "PRESET_TRACE event=reject target=2 reason=already_current confirmed=2",
        ])
        self.assertIsNone(final_synced_target([2], records, outcomes))
        record_trace("PRESET_TRACE event=startup_full_query id=0 target=2 msg=13 sent=1", records, outcomes)
        record_trace("PRESET_TRACE event=startup_full_result id=0 target=2 msg=12 sent=1 match=1 ready=1", records, outcomes)
        self.assertIsNone(final_synced_target([2], records, outcomes))


if __name__ == "__main__":
    unittest.main()
