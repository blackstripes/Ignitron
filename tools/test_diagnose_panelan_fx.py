import json
from contextlib import redirect_stderr
from io import StringIO
import unittest

from diagnose_panelan_fx import Diagnostic, SLOTS, fx_trace, parse_args, summary


def ready(d, t=0, phase='Ready', amp='Spark 2'):
    values = {'Spark connected': 'yes', 'Amp': amp, 'Serial': 'S', 'Preset name': 'P',
              'Active hardware slot': '1', 'Current preset': '1', 'Controller phase': phase,
              'identityKnown': 'true', 'sparkStateStale': 'false',
              'fullPresetObservedForLink': 'true', 'confirmedHardwarePreset': '1'}
    d.on_status(values, t)


def feed(d, line, t=1):
    d.on_line((line + '\n').encode(), t)


def start(d, slot=0, desired=True, model='M', t=0):
    assert d.next_command(t) == f'fx {SLOTS[slot]} toggle'
    feed(d, f'PRESET_TRACE event=fx_sent slot={slot} msg=23 desired={int(desired)}', t + .1)
    feed(d, f'Controller: sending FX {slot} ({model}) {"on" if desired else "off"}', t + .2)


def payload(d, desired=True, model='M', source='FX_ONOFF', t=1, msg=33):
    if source != 'FX_ONOFF':
        feed(d, f'PRESET_TRACE event=preset_parse_complete msg={msg} cmd=03 sub=01', t - .01)
    feed(d, 'Message processed:', t)
    data = {'Effect': model, 'IsOn': desired} if source == 'FX_ONOFF' else {
        'Pedals': [{'Name': model, 'IsOn': desired}]}
    feed(d, json.dumps(data), t + .01)


def confirm(d, slot=0, model='M', source='FX_ONOFF', t=2):
    feed(d, f'Controller: FX {slot} ({model}) confirmed by {source}', t)


def query(d, t=1):
    feed(d, 'PRESET_TRACE event=fx_full_query slot=0 msg=33 sent=1 first=1 ack=1 elapsed=500', t)


def result(d, match=1, enabled=1, msg=33, ack=1, t=1.2):
    feed(d, f'PRESET_TRACE event=fx_full_result slot=0 msg={msg} match={match} ack={ack} known=1 enabled={enabled} desired=1 chain=1 elapsed=600', t)


class FxDiagnosticTest(unittest.TestCase):
    def test_fx_sent_trace_after_known_effect_logger_prefix(self):
        plain = "PRESET_TRACE t=10 event=fx_sent slot=0 msg=23 desired=0"
        self.assertEqual(fx_trace(plain), fx_trace("Switching Off effect bias.noisegate..." + plain))
        self.assertEqual(fx_trace("> Switching Off effect bias.noisegate..." + plain), fx_trace(plain))
        self.assertIsNone(fx_trace("unrelated text contains " + plain))

        d = Diagnostic(count=1)
        ready(d)
        self.assertEqual(d.next_command(0), "fx gate toggle")
        feed(d, "Switching Off effect bias.noisegate...PRESET_TRACE t=10 event=fx_sent slot=0 msg=23 desired=0", .1)
        feed(d, "Controller: sending FX 0 (bias.noisegate) off", .2)
        payload(d, desired=False, model='bias.noisegate', t=.3)
        confirm(d, model='bias.noisegate', t=.4)
        self.assertIsNone(d.stop)
        self.assertEqual(d.actions[0]["msg"], "23")
        self.assertEqual(d.actions[0]["desired"], False)

    def setup_action(self, focus=False):
        d = Diagnostic(focus=focus, count=1, cadence=3)
        ready(d, phase='Syncing')
        self.assertIsNone(d.next_command(0))
        ready(d, amp='Spark')
        self.assertIsNone(d.next_command(0))
        ready(d)
        start(d)
        self.assertIsNone(d.stop)
        return d

    def test_raw_prompt_normalized_only_at_leading_edge(self):
        d = Diagnostic(count=1)
        for line in ('> --- Ignitron status ---', '> Spark connected: yes', '> Amp: Spark 2',
                     '> Serial: S', '> Preset name: P', '> Active hardware slot: 1',
                     '> Controller phase: Ready', '> identityKnown: true',
                     '> sparkStateStale: false', '> fullPresetObservedForLink: true',
                     '> confirmedHardwarePreset: 1', '> Looper loops: 0'):
            feed(d, line, 0)
        self.assertEqual(d.next_command(0), 'fx gate toggle')
        feed(d, '> PRESET_TRACE event=fx_sent slot=0 msg=23 desired=1')
        feed(d, '> Controller: sending FX 0 (M) on')
        payload(d)
        confirm(d)
        self.assertEqual(d.actions[0]['model'], 'M')

    def test_direct_and_full_fallback(self):
        d = self.setup_action()
        payload(d)
        confirm(d)
        self.assertEqual(d.actions[0]['source'], 'FX_ONOFF')
        ready(d, 3)
        d.next_command(3)
        self.assertEqual(d.stop, 'completed')
        d = self.setup_action()
        query(d)
        payload(d, source='full preset response')
        result(d)
        confirm(d, source='full preset response')
        self.assertEqual(d.actions[0]['query_msg'], '33')
        self.assertGreater(d.actions[0]['query_duration'], 0)

    def test_fresh_pre_ack_full_result_confirms_with_payload(self):
        d = self.setup_action()
        query(d)
        payload(d, source='full preset response')
        result(d, ack=0)
        confirm(d, source='full preset response')
        self.assertIsNone(d.stop)
        self.assertEqual(d.actions[0]['result']['ack'], '0')

    def test_stale_or_mismatched_query_does_not_confirm(self):
        for match, msg, payload_msg in ((0, 33, 33), (1, 34, 34), (1, 33, 34)):
            d = self.setup_action()
            query(d)
            payload(d, source='full preset response', msg=payload_msg)
            result(d, match=match, msg=msg, ack=0)
            confirm(d, source='full preset response')
            self.assertIn('incomplete', d.stop)
        d = self.setup_action()
        query(d)
        payload(d, False, source='full preset response', msg=34)
        result(d, match=0, enabled=0, msg=34, ack=1)
        self.assertIsNone(d.stop)
        payload(d, source='full preset response')
        result(d)
        confirm(d, source='full preset response')
        self.assertIsNone(d.stop)

    def test_pre_ack_old_state_inconclusive(self):
        d = self.setup_action()
        query(d)
        payload(d, False, source='full preset response')
        result(d, enabled=0, ack=0)
        self.assertIsNone(d.stop)
        self.assertIsNone(d.current['result'])
        feed(d, 'PRESET_TRACE event=fx_full_query slot=0 msg=34 sent=1 first=0 ack=1', 6)
        self.assertIn('retry', d.stop)

    def test_post_ack_old_state_conflicts(self):
        d = self.setup_action()
        query(d)
        payload(d, False, source='full preset response')
        result(d, enabled=0, ack=1)
        self.assertIn('conflict', d.stop)

    def test_ack_alone_does_not_confirm(self):
        d = self.setup_action()
        query(d)
        payload(d, source='full preset response')
        confirm(d, source='full preset response')
        self.assertIn('incomplete', d.stop)

    def test_focus_probes_and_rejections(self):
        d = self.setup_action(focus=True)
        self.assertEqual(d.probe_commands(), ['fx gate toggle', 'preset 1'])
        self.assertEqual(d.probe_commands(), [])
        feed(d, 'Effect command failed or was rejected.')
        feed(d, 'PRESET_TRACE event=reject target=1 reason=fx')
        feed(d, 'Preset request rejected; wait for synchronization to finish.')
        payload(d)
        confirm(d)
        self.assertIsNone(d.stop)
        self.assertEqual(d.actions[0]['probes'], {'duplicate', 'preset_cli', 'preset_trace'})
        d = self.setup_action(focus=True)
        payload(d)
        confirm(d)
        self.assertIsNotNone(d.stop)

    def test_focus_six_slots_probe_only_first_direction(self):
        d = Diagnostic(focus=True)
        ready(d)
        probes = []
        for n in range(12):
            slot = n // 2
            t = n * 4
            desired = n % 2 == 0
            start(d, slot, desired, SLOTS[slot], t)
            commands = d.probe_commands()
            probes.append(commands)
            self.assertEqual(commands, [f'fx {SLOTS[slot]} toggle', 'preset 1'] if desired else [])
            if commands:
                feed(d, 'Effect command failed or was rejected.', t + .3)
                feed(d, 'PRESET_TRACE event=reject target=1 reason=fx', t + .4)
                feed(d, 'Preset request rejected; wait for synchronization to finish.', t + .5)
            payload(d, desired, SLOTS[slot], t=t + 1)
            confirm(d, slot, SLOTS[slot], t=t + 2)
            self.assertIsNone(d.stop)
            ready(d, t + 3)
        self.assertEqual(sum(len(p) for p in probes), 12)
        self.assertEqual([a['probes'] for a in d.actions[1::2]], [set()] * 6)
        d.next_command(48)
        self.assertEqual(d.stop, 'completed')

    def test_fail_closed(self):
        failures = [
            ('Controller: FX 0 cancelled: timeout', 'cancelled'),
            ('PRESET_TRACE event=fx_full_query slot=0 msg=33 sent=1 first=0 ack=1', 'retry'),
            ('PRESET_TRACE event=fx_full_query slot=0 msg=0 sent=0 first=1 ack=0', 'failure'),
            ('PRESET_TRACE event=fx_full_query_deferred slot=0 desired=1 lastSubmissionStatus=Busy ownerSub=71', 'battery collision'),
            ('PRESET_TRACE event=battery_poll_sent', 'battery poll'),
            ('PRESET_TRACE event=ingress_trace_lost count=1', 'trace loss'),
            ('PRESET_TRACE event=disconnect', 'disconnect'),
            ('PRESET_TRACE event=frame_discard msg=23', 'anomaly'),
            ('PRESET_TRACE event=stream_message_reject', 'anomaly'),
        ]
        for line, reason in failures:
            with self.subTest(line=line):
                d = self.setup_action()
                feed(d, line)
                self.assertIn(reason, d.stop)
        d = self.setup_action()
        d.tick(21)
        self.assertIn('timeout', d.stop)
        d = self.setup_action()
        query(d)
        result(d, enabled=0)
        self.assertIn('conflict', d.stop)
        d = self.setup_action()
        ready(d, 1, amp='Spark')
        self.assertIn('lost', d.stop)

    def test_busy_other_owner_then_success(self):
        d = self.setup_action()
        feed(d, 'PRESET_TRACE event=fx_full_query_deferred slot=0 desired=1 lastSubmissionStatus=Busy ownerSub=01')
        query(d)
        payload(d, source='full preset response')
        result(d)
        confirm(d, source='full preset response')
        self.assertIsNone(d.stop)
        self.assertEqual(d.counters['busy_deferrals'], 1)
        report = summary(d, 3)
        self.assertEqual(report['deferred_owners'], {'01': 1})
        self.assertEqual(report['full_verifications'], 1)
        self.assertEqual(report['slots']['gate']['full_verifications'], 1)
        self.assertAlmostEqual(report['full_latency_p50'], .2)
        self.assertEqual(report['sources'], {'full preset response': 1})
        self.assertEqual(report['full_query_required'], 1)
        self.assertAlmostEqual(report['full_latency_p99'], .2)
        self.assertAlmostEqual(report['full_latency_max'], .2)

    def test_summary_observed_counters_and_slot_coverage(self):
        d = self.setup_action()
        feed(d, 'PRESET_TRACE event=battery_poll_deferred')
        feed(d, 'PRESET_TRACE event=fx_full_query_deferred slot=0 desired=1 lastSubmissionStatus=Busy ownerSub=01')
        query(d)
        payload(d, source='full preset response')
        result(d)
        confirm(d, source='full preset response')
        report = summary(d, 7)
        self.assertEqual((report['dispatched'], report['actions']), (1, 1))
        self.assertEqual(report['slots']['gate']['sources'], {'full preset response': 1})
        self.assertEqual(report['slots']['gate']['on'], 1)
        self.assertEqual(report['slots']['comp']['actions'], 0)
        self.assertEqual(report['battery_poll_deferred'], 1)
        self.assertEqual(report['events']['preset_parse_complete'], 1)
        self.assertEqual(report['full_query_failures'], 0)
        self.assertEqual(report['full_query_timeouts'], 0)
        self.assertEqual(report['full_query_retries'], 0)
        self.assertEqual(report['latency_p99'], report['latency_max'])
        self.assertEqual(report['slots']['gate']['full_latency_p99'], report['full_latency_max'])
        d = self.setup_action()
        query(d)
        d.tick(21)
        timed_out = summary(d, 21)
        self.assertEqual(timed_out['full_query_timeouts'], 1)
        self.assertEqual(timed_out['full_query_required'], 1)
        d = self.setup_action()
        feed(d, 'PRESET_TRACE event=fx_full_query slot=0 msg=0 sent=0 first=1 ack=0')
        self.assertEqual(summary(d, 1)['full_query_failures'], 1)
        d = self.setup_action()
        query(d)
        feed(d, 'PRESET_TRACE event=fx_full_query slot=0 msg=34 sent=1 first=0 ack=1')
        self.assertEqual(summary(d, 6)['full_query_retries'], 1)
        d = self.setup_action()
        feed(d, 'PRESET_TRACE event=stream_message_reject')
        self.assertEqual(summary(d, 1)['anomalies']['stream_message_reject'], 1)

    def test_cycle_inverse_and_model(self):
        d = Diagnostic(count=13, cadence=3)
        ready(d)
        for n in range(13):
            slot = n % 6
            t = n * 4
            desired = n < 6 or n >= 12
            start(d, slot, desired, SLOTS[slot], t)
            payload(d, desired, SLOTS[slot], t=t + 1)
            confirm(d, slot, SLOTS[slot], t=t + 2)
            self.assertIsNone(d.stop)
            ready(d, t + 3)
        d.next_command(52)
        self.assertEqual(d.stop, 'completed')
        self.assertEqual(len(d.actions), 13)
        d = self.setup_action()
        payload(d)
        confirm(d)
        ready(d, 3)
        # Long mode cycles slots; force next visit to slot 0.
        d.actions.extend([dict(slot=i) for i in range(1, 6)])
        d.count = 13
        start(d, 0, True, 'M', 4)
        self.assertIn('inverse', d.stop)

    def test_count_bounds(self):
        self.assertEqual(parse_args(['--port', 'p', '--count', '500']).count, 500)
        for count in ('0', '501'):
            with redirect_stderr(StringIO()), self.assertRaises(SystemExit):
                parse_args(['--port', 'p', '--count', count])

    def test_500_dispatches_cycle_and_state(self):
        d = Diagnostic(count=500)
        ready(d)
        for n in range(500):
            t = n * 4
            slot = n % 6
            desired = (n // 6) % 2 == 0
            start(d, slot, desired, SLOTS[slot], t)
            payload(d, desired, SLOTS[slot], t=t + 1)
            confirm(d, slot, SLOTS[slot], t=t + 2)
            self.assertIsNone(d.stop)
            ready(d, t + 3)
        d.next_command(2000)
        self.assertEqual(d.stop, 'completed')
        self.assertEqual(len(d.actions), 500)
        report = summary(d, 2000)
        self.assertEqual(report['runtime_seconds'], 2000)
        self.assertEqual((report['dispatched'], report['actions']), (500, 500))
        self.assertEqual(sum(s['on'] + s['off'] for s in report['slots'].values()), 500)
        self.assertEqual(report['slots']['gate']['dispatched'], 84)
        self.assertEqual(report['slots']['comp']['dispatched'], 84)
        self.assertEqual(report['slots']['reverb']['dispatched'], 83)
        self.assertEqual(report['slots']['gate']['on'], 42)
        self.assertEqual(report['sources'], {'FX_ONOFF': 500})
        self.assertAlmostEqual(report['latency_p95'], 1.8)
        self.assertEqual(report['full_verifications'], 0)
        self.assertEqual(report['battery_polls'], 0)

    def test_model_change_and_focus_preset_change(self):
        d = self.setup_action()
        payload(d)
        confirm(d)
        ready(d, 3)
        d.actions.extend([dict(slot=i) for i in range(1, 6)])
        d.count = 13
        start(d, 0, False, 'Other', 4)
        self.assertIn('model changed', d.stop)
        d = self.setup_action(focus=True)
        d.probe_commands()
        changed = dict(d.last_status, **{'confirmedHardwarePreset': '2'})
        d.on_status(changed, 1)
        self.assertIn('preset changed', d.stop)


if __name__ == '__main__':
    unittest.main()
