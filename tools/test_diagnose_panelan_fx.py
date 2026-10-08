import json
from collections import deque
from contextlib import redirect_stderr
from io import StringIO
import unittest

from diagnose_panelan_fx import (Diagnostic, MAX_READY_AGE, SLOTS, fx_status,
                                 cli_prompt_only_buffer, command_write_window_clear, fx_ready, fx_trace,
                                  parse_args, summary)


def ready_slots():
    return {name: dict(known=True, model='M', enabled=False, pending=False, failed=False)
            for name in SLOTS}


def ready(d, t=0, phase='Ready', amp='Spark 2', requested_at=None, models=None, **changes):
    values = {'Spark connected': 'yes', 'Amp': amp, 'Serial': 'S', 'Preset name': 'P',
              'Active hardware slot': '1', 'Current preset': '1', 'Controller phase': phase,
              'identityKnown': 'true', 'sparkStateStale': 'false',
              'fullPresetObservedForLink': 'true', 'confirmedHardwarePreset': '1',
               'pendingHardwarePreset': '0', 'presetActionFailed': 'false', 'FX chain': 'chain',
               'FX slots': {name: dict(known=True, model=(models or ['M'] * 6)[i],
                                        enabled=d.states.get(i, False), pending=False, failed=False)
                            for i, name in enumerate(SLOTS)}}
    d.on_status(dict(values, **changes), t, t if requested_at is None else requested_at)


def status_lines(models=None, enabled=None, chain='chain'):
    return ('--- Ignitron status ---', 'Spark connected: yes', 'Amp: Spark 2',
            'Serial: S', 'Preset name: P', 'Active hardware slot: 1',
            'Controller phase: Ready', 'identityKnown: true', 'sparkStateStale: false',
            'fullPresetObservedForLink: true', 'confirmedHardwarePreset: 1',
            'pendingHardwarePreset: 0', 'presetActionFailed: false',
            'lastSubmissionStatus: Sent', 'currentCommand hasRemaining: false  remainingParts: 0',
            'responseLane active: false  owner msg: 0  owner sub: 00',
            'controllerFullPresetMessageNumber: 0', f'FX chain: {chain}',
            *(f'FX {name}: known=true model={(models or ["M"] * 6)[i]} enabled={"true" if (enabled or [False] * 6)[i] else "false"} pending=false failed=false'
              for i, name in enumerate(SLOTS)), 'Looper loops: 0')


def feed(d, line, t=1, requests=None):
    d.on_line((line + '\n').encode(), t, requests)


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
    def test_fx_status_parses_complete_block_and_readonly_baseline(self):
        values = fx_status(status_lines(models=['bias.noisegate'] + ['M'] * 5,
                                        enabled=[True] + [False] * 5))
        self.assertEqual(values['FX chain'], 'chain')
        self.assertEqual(values['FX slots']['gate'], dict(known=True, model='bias.noisegate',
                                                         enabled=True, pending=False, failed=False))
        d = Diagnostic(count=1)
        d.on_status(values, 0, 0)
        self.assertTrue(d.ready)
        self.assertEqual(d.baseline['slots']['gate']['enabled'], True)
        self.assertEqual(d.baseline['chain'], 'chain')
        self.assertEqual(d.actions, [])  # a status read sends no FX
        self.assertIsNone(d.current)

    def test_fx_ready_rejects_missing_unknown_pending_and_failed_slots(self):
        base = fx_status(status_lines())
        for change in ({'FX chain': '(unknown)'},
                       {'FX slots': {**base['FX slots'], 'gate': dict(base['FX slots']['gate'], known=False)}},
                       {'FX slots': {**base['FX slots'], 'comp': dict(base['FX slots']['comp'], model='(unknown)')}},
                       {'FX slots': {**base['FX slots'], 'drive': dict(base['FX slots']['drive'], model='')}},
                       {'FX slots': {**base['FX slots'], 'mod': dict(base['FX slots']['mod'], pending=True)}},
                       {'FX slots': {**base['FX slots'], 'delay': dict(base['FX slots']['delay'], failed=True)}},
                       {'FX slots': {k: v for k, v in base['FX slots'].items() if k != 'reverb'}}):
            with self.subTest(change=change):
                self.assertFalse(fx_ready(dict(base, **change)))
        self.assertFalse(fx_ready(fx_status(status_lines()[:-2] + ('Looper loops: 0',))))
        self.assertIsNone(fx_status(status_lines()[:-1]))

    def test_pin_all_models_and_chain_even_when_not_ready(self):
        for change, reason in (({'FX chain': 'other'}, 'chain changed'),
                               ({'FX slots': dict(ready_slots(), reverb=dict(ready_slots()['reverb'], model='other'))}, 'model changed')):
            d = Diagnostic(count=2)
            ready(d)
            ready(d, 1, phase='Syncing', **change)
            self.assertIn(reason, d.stop)

    def test_first_gate_direction_inverse_of_baseline_and_post_confirm_state(self):
        for baseline, desired in ((True, False), (False, True)):
            d = Diagnostic(count=2)
            ready(d, **{'FX slots': dict(ready_slots(), gate=dict(ready_slots()['gate'], enabled=baseline))})
            start(d, desired=desired)
            self.assertIsNone(d.stop)
            payload(d, desired=desired)
            confirm(d)
            self.assertFalse(d.ready)
            ready(d, 3, requested_at=1.5)
            self.assertIsNone(d.next_command(3))
            bad = dict(ready_slots(), gate=dict(ready_slots()['gate'], enabled=baseline))
            ready(d, 3.1, **{'FX slots': bad})
            self.assertIn('state differs', d.stop)
            self.assertIsNone(d.next_command(3.1))
        d = Diagnostic(count=1)
        ready(d, **{'FX slots': dict(ready_slots(), gate=dict(ready_slots()['gate'], enabled=True))})
        start(d, desired=True)
        self.assertIn('inverse', d.stop)

    def test_fx_ready_gate_rejects_pending_or_failed_preset(self):
        values = {'Spark connected': 'yes', 'Amp': 'Spark 2', 'Serial': 'S', 'Preset name': 'P',
                  'Active hardware slot': '1', 'Current preset': '1', 'Controller phase': 'Ready',
                  'identityKnown': 'true', 'sparkStateStale': 'false', 'fullPresetObservedForLink': 'true',
                  'confirmedHardwarePreset': '1', 'pendingHardwarePreset': '0', 'presetActionFailed': 'false'}
        values.update({'FX chain': 'chain', 'FX slots': ready_slots()})
        self.assertTrue(fx_ready(values))
        self.assertFalse(fx_ready(dict(values, pendingHardwarePreset='2')))
        self.assertFalse(fx_ready(dict(values, presetActionFailed='true')))

    def test_command_window_waits_for_buffered_input_and_pending_status(self):
        self.assertTrue(command_write_window_clear(False, False, False, False))
        for state in ((True, False, False, False), (False, True, False, False),
                      (False, False, True, False), (False, False, False, True)):
            with self.subTest(state=state):
                self.assertFalse(command_write_window_clear(*state))
        self.assertTrue(cli_prompt_only_buffer(b"> "))
        self.assertTrue(cli_prompt_only_buffer(b"> >  "))
        self.assertTrue(command_write_window_clear(False, not cli_prompt_only_buffer(b"> "), False, False))
        self.assertFalse(cli_prompt_only_buffer(b"> PRESET_TRACE t=1 event=accept"))

    def test_fx_sent_trace_after_known_effect_logger_prefix(self):
        plain = "PRESET_TRACE t=10 event=fx_sent slot=0 msg=23 desired=0"
        self.assertEqual(fx_trace(plain), fx_trace("Switching Off effect bias.noisegate..." + plain))
        self.assertEqual(fx_trace("> Switching Off effect bias.noisegate..." + plain), fx_trace(plain))
        self.assertIsNone(fx_trace("unrelated text contains " + plain))

        d = Diagnostic(count=1)
        ready(d, **{'FX slots': dict(ready_slots(), gate=dict(ready_slots()['gate'], model='bias.noisegate', enabled=True)),
                    'FX chain': 'chain'})
        self.assertEqual(d.next_command(0), "fx gate toggle")
        feed(d, "Switching Off effect bias.noisegate...PRESET_TRACE t=10 event=fx_sent slot=0 msg=23 desired=0", .1)
        feed(d, "Controller: sending FX 0 (bias.noisegate) off", .2)
        payload(d, desired=False, model='bias.noisegate', t=.3)
        confirm(d, model='bias.noisegate', t=.4)
        self.assertIsNone(d.stop)
        self.assertEqual(d.actions[0]["msg"], "23")
        self.assertEqual(d.actions[0]["desired"], False)

    def test_first_direction_comes_from_controller_not_harness(self):
        d = Diagnostic(count=1)
        ready(d, **{'FX slots': dict(ready_slots(), gate=dict(ready_slots()['gate'], enabled=True))})
        start(d, desired=False)
        payload(d, desired=False)
        confirm(d)
        self.assertIsNone(d.stop)
        self.assertFalse(d.actions[0]['desired'])

    def test_prompt_embedded_missing_trace_send_counts_as_dispatched(self):
        d = Diagnostic(count=1)
        ready(d, **{'FX slots': dict(ready_slots(), gate=dict(ready_slots()['gate'], enabled=True))})
        self.assertEqual(d.next_command(0), 'fx gate toggle')
        feed(d, '> unrelated prefix PRESET_TRACE event=fx_sent slot=0 msg=23 desired=0', .1)
        self.assertEqual(d.counters['event_fx_sent'], 0)
        feed(d, '> Controller: sending FX 0 (bias.noisegate) off', .2)
        self.assertIn('lacks matching fx_sent', d.stop)
        self.assertEqual((summary(d, .2)['dispatched'], summary(d, .2)['actions']), (1, 0))
        self.assertEqual(summary(d, .2)['slots']['gate']['dispatched'], 1)
        self.assertTrue(d.current['send_seen'])
        self.assertIsNone(d.current['msg'])
        self.assertIsNone(d.current['source'])
        self.assertIsNone(d.next_command(4))

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
        requests = deque([0])
        for line in status_lines():
            feed(d, line, 0, requests)
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
        ready(d, models=SLOTS)
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
            ready(d, t + 3, models=SLOTS)
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

    def test_battery_poll_after_confirm_completion_and_idle_status_is_valid(self):
        d = self.setup_action()
        payload(d)
        confirm(d)
        self.assertIsNone(d.current)  # authoritative confirmation completes action
        requests = deque([3])
        for line in status_lines(enabled=[True] + [False] * 5):
            feed(d, line, 3, requests)
        self.assertTrue(d.ready)
        self.assertEqual(d.last_status['responseLane active'], 'false')
        self.assertFalse(any(slot['pending'] for slot in d.last_status['FX slots'].values()))
        feed(d, 'PRESET_TRACE event=battery_poll_sent lastSubmissionStatus=Sent '
                'currentCommandHasRemaining=0 currentCommandRemainingParts=0 '
                'responseLaneActive=1 ownerMsg=70 ownerSub=71', 3.1)
        self.assertIsNone(d.stop)
        self.assertEqual(d.actions[0]['source'], 'FX_ONOFF')
        self.assertEqual(d.counters['event_battery_poll_sent'], 1)

    def test_battery_poll_before_controller_fx_send_is_not_collision(self):
        d = Diagnostic(count=1)
        ready(d)
        self.assertEqual(d.next_command(0), 'fx gate toggle')
        self.assertIsNone(d.current['msg'])
        self.assertFalse(d.current['send_seen'])
        feed(d, 'PRESET_TRACE event=battery_poll_sent ownerSub=71', .1)
        self.assertIsNone(d.stop)
        feed(d, 'PRESET_TRACE event=fx_sent slot=0 msg=23 desired=1', .2)
        feed(d, 'Controller: sending FX 0 (M) on', .3)
        feed(d, 'PRESET_TRACE event=battery_poll_sent ownerSub=71', .4)
        self.assertIn('battery poll sent while FX pending', d.stop)

    def test_battery_poll_owning_fx_verification_lane_still_fails(self):
        d = self.setup_action()
        feed(d, 'PRESET_TRACE event=fx_full_query_deferred slot=0 desired=1 '
                'lastSubmissionStatus=Busy ownerSub=71')
        self.assertIn('battery collision', d.stop)

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
        ready(d, models=SLOTS)
        for n in range(13):
            slot = n % 6
            t = n * 4
            desired = n < 6 or n >= 12
            start(d, slot, desired, SLOTS[slot], t)
            payload(d, desired, SLOTS[slot], t=t + 1)
            confirm(d, slot, SLOTS[slot], t=t + 2)
            self.assertIsNone(d.stop)
            ready(d, t + 3, models=SLOTS)
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
        ready(d, models=SLOTS)
        for n in range(500):
            t = n * 4
            slot = n % 6
            desired = (n // 6) % 2 == 0
            start(d, slot, desired, SLOTS[slot], t)
            payload(d, desired, SLOTS[slot], t=t + 1)
            confirm(d, slot, SLOTS[slot], t=t + 2)
            self.assertIsNone(d.stop)
            ready(d, t + 3, models=SLOTS)
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

    def test_identity_and_preset_pin_between_actions_and_while_pending(self):
        for focus in (False, True):
            for change in ({'Serial': 'Other'}, {'Preset name': 'Other'},
                           {'Current preset': '2', 'Active hardware slot': '2', 'confirmedHardwarePreset': '2'},
                           {'confirmedHardwarePreset': '2', 'Controller phase': 'Syncing'}):
                with self.subTest(focus=focus, change=change):
                    d = Diagnostic(focus=focus, count=2)
                    ready(d)
                    start(d)
                    if focus:
                        d.probe_commands()
                        ready(d, 1, **change)
                    else:
                        payload(d)
                        confirm(d)
                        ready(d, 3, **change)
                    self.assertIn('changed', d.stop)
                    self.assertIsNone(d.next_command(4))
                    self.assertEqual(summary(d, 4)['dispatched'], 1)

    def test_complete_status_block_catches_changed_serial_between_actions(self):
        d = Diagnostic(count=2)
        ready(d)
        start(d)
        payload(d)
        confirm(d)
        lines = tuple(line.replace('Serial: S', 'Serial: Other') for line in status_lines(enabled=[True] + [False] * 5))
        requests = deque([3])
        for line in lines:
            feed(d, line, 3, requests)
        self.assertIn('changed', d.stop)
        self.assertIsNone(d.next_command(3))

    def test_stale_ready_blocks_dispatch_until_fresh_complete_status(self):
        d = Diagnostic(count=2)
        ready(d)
        self.assertIsNone(d.next_command(MAX_READY_AGE + .01))
        self.assertIsNone(d.current)
        ready(d, 2, phase='Syncing')
        self.assertIsNone(d.next_command(2))
        ready(d, 3)
        start(d, t=3)
        payload(d, t=4)
        confirm(d, t=4.1)
        self.assertIsNone(d.next_command(6))  # no new Ready after first action
        ready(d, 7)
        self.assertIsNone(d.next_command(7 + MAX_READY_AGE + .01))
        ready(d, 9)
        self.assertEqual(d.next_command(9), 'fx comp toggle')

    def test_ready_seen_during_action_cannot_gate_following_dispatch(self):
        d = Diagnostic(count=2, cadence=3)
        ready(d)
        start(d)
        payload(d)
        ready(d, 1)  # a status concurrent with the in-flight FX operation
        confirm(d, t=1.1)
        self.assertIsNone(d.next_command(3))
        ready(d, 3.1)  # a fresh complete snapshot after confirmation
        self.assertEqual(d.next_command(3.1), 'fx comp toggle')

    def test_status_block_started_during_action_is_discarded_on_confirmation(self):
        d = Diagnostic(count=2, cadence=3)
        ready(d)
        start(d)
        for line in status_lines()[:-1]:
            feed(d, line, .5)
        payload(d)
        confirm(d, t=1.1)
        feed(d, "Looper loops: 0", 1.2)
        self.assertIsNone(d.next_command(3.1))
        ready(d, 3.2)
        self.assertEqual(d.next_command(3.2), 'fx comp toggle')

    def test_pre_confirm_request_completing_after_confirm_cannot_gate(self):
        d = Diagnostic(count=2)
        ready(d)
        start(d)
        requests = deque([.5])
        for line in status_lines()[:-1]:
            feed(d, line, 1, requests)
        payload(d, t=1.2)
        confirm(d, t=2)
        feed(d, 'Looper loops: 0', 2.1, requests)
        self.assertEqual(len(requests), 0)
        self.assertFalse(d.ready)
        self.assertIsNone(d.next_command(3))
        requests.append(2.5)
        for line in status_lines(enabled=[True] + [False] * 5):
            feed(d, line, 3, requests)
        self.assertEqual(d.next_command(3), 'fx comp toggle')

    def test_preconfirm_block_tail_cannot_consume_postconfirm_status_request(self):
        d = Diagnostic(count=2, cadence=3)
        ready(d)
        start(d)
        requests = deque([.5])
        fields = status_lines(enabled=[True] + [False] * 5)[1:-1]
        feed(d, '--- Ignitron status ---', 1, requests)
        for line in fields:
            feed(d, line, 1, requests)
        payload(d, t=1.2)
        confirm(d, t=2)
        requests.append(2.1)  # main issues a fresh status after confirmation
        feed(d, 'Looper loops: 0', 2.2, requests)  # tail of old pre-confirm block
        self.assertEqual(list(requests), [2.1])
        self.assertFalse(d.ready)
        self.assertIsNone(d.next_command(3))
        for line in ('--- Ignitron status ---', *fields, 'Looper loops: 0'):
            feed(d, line, 3.1, requests)
        self.assertEqual(list(requests), [])
        self.assertEqual(d.next_command(3.1), 'fx comp toggle')

    def test_unsolicited_ready_cannot_gate_initial_or_post_confirm_action(self):
        d = Diagnostic(count=2)
        ready(d)
        start(d)
        payload(d)
        confirm(d)
        d.on_status(d.last_status, 3)
        self.assertIsNone(d.next_command(3))
        ready(d, 3.1, requested_at=1.5)
        self.assertIsNone(d.next_command(3.1))
        ready(d, 3.2, requested_at=2)
        self.assertEqual(d.next_command(3.2), 'fx comp toggle')
        initial = Diagnostic(count=1)
        initial.on_status(d.last_status, 0)
        self.assertIsNone(initial.next_command(0))

    def test_completed_status_consumes_oldest_request_and_checks_uncorrelated_pin(self):
        d = Diagnostic(count=1)
        requests = deque([0, 1])
        lines = status_lines()
        for line in lines:
            feed(d, line, 2, requests)
        self.assertEqual(list(requests), [1])
        self.assertEqual(d.next_command(2), 'fx gate toggle')
        for line in lines:
            feed(d, line, 2.1, requests)
        self.assertEqual(list(requests), [])
        for line in lines:
            feed(d, line.replace('Serial: S', 'Serial: Other'), 2.2, requests)
        self.assertIn('changed', d.stop)

    def test_expired_ready_snapshot_eventually_times_out_without_dispatch(self):
        d = Diagnostic(count=1, ready_timeout=3)
        ready(d, 0)
        self.assertIsNone(d.next_command(MAX_READY_AGE + .01))
        d.tick(MAX_READY_AGE + 3)
        self.assertIn('Ready timeout', d.stop)
        self.assertIsNone(d.current)

    def test_fresh_status_resets_wait_after_expiry_then_no_response_times_out(self):
        d = Diagnostic(count=1, ready_timeout=3)
        d.tick(0)
        ready(d, 2)
        d.tick(2)
        d.tick(2 + MAX_READY_AGE + 2.9)
        self.assertIsNone(d.stop)
        d.tick(2 + MAX_READY_AGE + 3)
        self.assertIn('Ready timeout', d.stop)
        self.assertIsNone(d.next_command(10))

    def test_non_finite_timing_arguments_rejected(self):
        for flag in ('--ready-timeout', '--action-timeout', '--cadence'):
            for value in ('nan', 'inf', '-inf'):
                with self.subTest(flag=flag, value=value):
                    with redirect_stderr(StringIO()), self.assertRaises(SystemExit):
                        parse_args(['--port', 'p', '--count', '1', flag, value])


if __name__ == '__main__':
    unittest.main()
