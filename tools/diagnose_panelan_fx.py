#!/usr/bin/env python3
"""Trace-firmware Spark 2 FX diagnostic. Operator supplies the CDC port; never retries an action."""
import argparse
from collections import Counter
from datetime import datetime
import json
import os
import re
import select
import termios
import time

from diagnose_panelan_presets import normalize_cli_line, snapshot_ready, status, trace

SLOTS = ("gate", "comp", "drive", "mod", "delay", "reverb")
SEND = re.compile(r"Controller: sending FX ([0-5]) \(([^()]+)\) (on|off)")
CONFIRM = re.compile(r"Controller: FX ([0-5]) \(([^()]+)\) confirmed by (FX_ONOFF|full preset response)")
LEGACY_EFFECT_PREFIX = re.compile(r"^Switching (?:On|Off) effect [^\r\n]*\.\.\.(PRESET_TRACE .*)$")


def fx_trace(line):
    """Parse a normal trace line or the known no-newline effect logger prefix."""
    line = normalize_cli_line(line)
    event = trace(line)
    if event is not None:
        return event
    prefixed = LEGACY_EFFECT_PREFIX.match(line)
    return trace(prefixed.group(1)) if prefixed else None


class Diagnostic:
    """Hardware-free monotonic event machine; feed complete raw serial lines in arrival order."""
    def __init__(self, focus=False, count=150, cadence=3, ready_timeout=60, action_timeout=20):
        self.focus, self.count = focus, count
        self.cadence, self.ready_timeout, self.action_timeout = cadence, ready_timeout, action_timeout
        self.started = False
        self.ready = False
        self.ready_since = None
        self.wait_since = None
        self.last_send = float('-inf')
        self.actions = []
        self.current = None
        self.models = {}
        self.states = {}
        self.blocks = []
        self.payload = None
        self.parse_msg = None
        self.payload_msg = None
        self.probed_slots = set()
        self.stop = None
        self.counters = Counter()
        self.last_status = {}

    def fail(self, reason):
        if self.stop is None:
            self.stop = reason
            if 'model changed' in reason or 'target model' in reason:
                self.counters['model_conflicts'] += 1
            if 'chain changed' in reason:
                self.counters['chain_conflicts'] += 1

    def on_status(self, values, now):
        if values is None:
            return
        if self.current and self.focus and self.current['probe_sent'] and self.last_status:
            for key in ('confirmedHardwarePreset', 'Preset name', 'Active hardware slot'):
                if values.get(key) != self.last_status.get(key):
                    self.fail('amp preset changed while rejection probes pending')
        self.last_status = values
        if self.started and (values.get('Spark connected') != 'yes' or values.get('Amp') != 'Spark 2'):
            self.counters['connection_status_lost'] += 1
            self.fail('Spark connection/identity lost')
        self.ready = snapshot_ready(values)
        if self.ready:
            self.started = True
            self.ready_since = now
        elif self.current is None:
            self.ready_since = None

    def next_command(self, now):
        if self.stop or self.current or not self.ready or self.ready_since is None:
            return None
        if len(self.actions) >= (12 if self.focus else self.count):
            self.stop = 'completed'
            return None
        if now - self.last_send < self.cadence:
            return None
        slot = (len(self.actions) // 2 if self.focus else len(self.actions)) % 6
        probe_required = self.focus and slot not in self.probed_slots
        self.current = dict(slot=slot, model=None, desired=None, at=now, sent_at=None,
                            msg=None, source=None, latency=None, query_msg=None,
                            query_duration=None, query_at=None, retries=0, deferrals=0,
                            events=[], payloads=[], result=None, probes=set(), probe_sent=False,
                            probe_required=probe_required)
        self.ready = False  # require a new authoritative Ready after every action
        self.ready_since = None
        self.last_send = now
        self.wait_since = None
        return f'fx {SLOTS[slot]} toggle'

    def probe_commands(self):
        a = self.current
        if a and a['probe_required'] and a['sent_at'] is not None and not a['probe_sent']:
            a['probe_sent'] = True
            self.probed_slots.add(a['slot'])
            return [f"fx {SLOTS[a['slot']]} toggle", 'preset 1']
        return []

    def tick(self, now):
        a = self.current
        if a and now - a['at'] >= self.action_timeout:
            self.fail('FX action timeout')
        if not a and self.stop is None and not self.ready:
            if self.wait_since is None:
                self.wait_since = now
            if now - self.wait_since >= self.ready_timeout:
                self.fail('authoritative Spark 2 Ready timeout')

    def on_payload(self, data):
        a = self.current
        if not a or a['sent_at'] is None or not isinstance(data, dict):
            return
        matches = []
        if 'Effect' in data and type(data.get('IsOn')) is bool:
            matches.append(('FX_ONOFF', data['Effect'], data['IsOn']))
        if isinstance(data.get('Pedals'), list):
            matches.extend(('full preset response', p.get('Name'), p['IsOn']) for p in data['Pedals']
                           if isinstance(p, dict) and type(p.get('IsOn')) is bool)
        for source, model, enabled in matches:
            if model == a['model']:
                if source == 'FX_ONOFF' and enabled != a['desired']:
                    self.fail('conflicting Spark payload for target model')
                a['payloads'].append((source, enabled, self.payload_at, self.payload_msg))

    def on_line(self, raw, now):
        line = normalize_cli_line(raw.decode(errors='replace').rstrip('\r\n'))
        if line == 'Message processed:':
            self.payload = ''
            self.payload_at = now
            self.payload_msg, self.parse_msg = self.parse_msg, None
        elif self.payload is not None:
            self.payload += line + '\n'
            try:
                data = json.loads(self.payload)
            except json.JSONDecodeError:
                if len(self.payload) > 65536:
                    self.fail('Spark payload exceeds parser limit')
            else:
                self.payload = None
                self.on_payload(data)
        if line == '--- Ignitron status ---':
            self.blocks = [line]
        elif self.blocks:
            self.blocks.append(line)
            if line.startswith('Looper loops:'):
                self.on_status(status(self.blocks), now)
                self.blocks = []
        e = fx_trace(line)
        kind = e.get('event') if e else None
        if kind:
            self.counters['event_' + kind] += 1
        if kind == 'preset_parse_complete':
            self.parse_msg = e.get('msg')
        if kind == 'ingress_trace_lost':
            self.fail('trace loss')
        if kind == 'disconnect' or line == 'Spark amp is not connected.':
            if line == 'Spark amp is not connected.':
                self.counters['connection_cli_disconnected'] += 1
            self.fail('Spark disconnect')
        a = self.current
        if not a or self.stop:
            return
        if e:
            a['events'].append(dict(e, at=now))
        if kind == 'battery_poll_sent':
            self.fail('battery poll sent while FX pending')
        if kind in ('frame_discard', 'multipart_discard', 'preset_parse_reject', 'incoming_reject',
                    'stream_message_reject', 'stream_discard', 'multipart_timeout'):
            self.fail('relevant transport/parser anomaly: ' + str(e))
        if 'Controller: FX ' in line and ('cancelled:' in line or 'failed' in line):
            self.fail(line)
        sent = SEND.search(line)
        if sent:
            if a['model'] is not None or int(sent[1]) != a['slot']:
                self.fail('unexpected FX send: ' + line)
            else:
                a['model'], direction = sent[2], sent[3]
                a['desired'] = direction == 'on'
                if a['msg'] is None or a['desired'] != a.get('trace_desired'):
                    self.fail('FX send lacks matching fx_sent trace')
                if a['slot'] in self.models and self.models[a['slot']] != a['model']:
                    self.fail('FX model changed')
                if a['slot'] in self.states and self.states[a['slot']] == a['desired']:
                    self.fail('FX direction not inverse of last confirmed state')
                a['sent_at'] = now
        if kind == 'fx_sent':
            if a['msg'] is not None or e.get('slot') != str(a['slot']) or e.get('desired') not in ('0', '1'):
                self.fail('unexpected fx_sent: ' + str(e))
            else:
                a['msg'], a['trace_desired'] = e['msg'], e['desired'] == '1'
                a['events'][-1]['at'] = now
        if kind == 'fx_full_query_deferred':
            if e.get('slot') != str(a['slot']) or e.get('desired') != str(int(a['desired'])) or e.get('lastSubmissionStatus') != 'Busy':
                self.fail('invalid deferred FX query: ' + str(e))
            elif e.get('ownerSub', '').upper() == '71':
                self.fail('battery collision: ' + str(e))
            else:
                a['deferrals'] += 1
                self.counters['busy_deferrals'] += 1
                self.counters['busy_owner_' + e.get('ownerSub', 'unknown').upper()] += 1
        if kind == 'fx_full_query':
            if e.get('first') != '1' or a['query_msg'] is not None:
                a['retries'] += 1
            if e.get('slot') != str(a['slot']) or e.get('sent') != '1':
                self.fail('FX full query send failure: ' + str(e))
            elif a['retries']:
                self.fail('FX full query retry: ' + str(e))
            else:
                a['query_msg'], a['query_at'] = e.get('msg'), now
        if kind == 'fx_full_result' and e.get('slot') == str(a['slot']):
            if e.get('match') == '1' and e.get('msg') == a['query_msg'] and e.get('chain') == '0':
                self.fail('FX chain changed')
            if e.get('match') == '1' and e.get('msg') == a['query_msg'] and a['query_at'] is not None:
                if e.get('known') != '1' or e.get('desired') != str(int(a['desired'])):
                    self.fail('matching full result conflict: ' + str(e))
                elif e.get('enabled') == str(int(a['desired'])):
                    a['result'] = e
                    a['query_duration'] = now - a['query_at']
                elif e.get('ack') == '1':
                    self.fail('matching full result conflict: ' + str(e))
        if self.focus and a['probe_sent']:
            if line == 'Effect command failed or was rejected.':
                a['probes'].add('duplicate')
            if line == 'Preset request rejected; wait for synchronization to finish.':
                a['probes'].add('preset_cli')
            if kind == 'reject' and e.get('target') == '1' and e.get('reason') == 'fx':
                a['probes'].add('preset_trace')
            if kind == 'accept' or re.search(r'Preset \d+ (requested|selected)', line):
                self.fail('preset accepted while FX pending')
            if line == 'Effect toggle requested.' and a['sent_at'] is not None:
                self.fail('busy duplicate accepted')
        if line == 'Effect command failed or was rejected.' and not (self.focus and a['probe_sent']):
            self.fail('FX command rejected')
        if 'Current preset data is not available' in line:
            self.fail('FX snapshot unavailable')
        confirmed = CONFIRM.search(line)
        if confirmed and not self.stop:
            source = confirmed[3]
            if (a['sent_at'] is None or int(confirmed[1]) != a['slot'] or confirmed[2] != a['model'] or
                    not any(kind == source and enabled == a['desired'] and
                            (source == 'FX_ONOFF' or
                             (a['result'] is not None and msg == a['query_msg'] and
                              a['result'].get('msg') == msg and a['query_at'] is not None and at >= a['query_at']))
                            for kind, enabled, at, msg in a['payloads']) or
                    (a['probe_required'] and
                      a['probes'] != {'duplicate', 'preset_cli', 'preset_trace'})):
                self.fail('uncorrelated/incomplete FX confirmation: ' + line)
            else:
                a['source'], a['latency'] = source, now - a['sent_at']
                self.models[a['slot']] = a['model']
                self.states[a['slot']] = a['desired']
                self.actions.append(a)
                self.counters[source] += 1
                self.current = None
                self.wait_since = now


def percentile(values, percent):
    if not values:
        return None
    ordered = sorted(values)
    position = (len(ordered) - 1) * percent / 100
    lower = int(position)
    return ordered[lower] + (ordered[min(lower + 1, len(ordered) - 1)] - ordered[lower]) * (position - lower)


def latency_stats(values, prefix):
    return {f'{prefix}_{key}': percentile(values, rank) for key, rank in
            (('p50', 50), ('p95', 95), ('p99', 99), ('max', 100))}


def summary(diagnostic, runtime):
    """Aggregate only observed sends/confirmations; never infer success from ACKs."""
    actions = diagnostic.actions
    pending = diagnostic.current
    dispatched = actions + ([pending] if pending and pending['msg'] is not None else [])
    slots = {}
    for index, name in enumerate(SLOTS):
        confirmed = [a for a in actions if a['slot'] == index]
        latencies = [a['latency'] for a in confirmed]
        full = [a['query_duration'] for a in confirmed if a['query_duration'] is not None]
        slots[name] = dict(dispatched=sum(a['slot'] == index for a in dispatched),
                            actions=len(confirmed), on=sum(a['desired'] for a in confirmed),
                            off=sum(not a['desired'] for a in confirmed),
                            sources=dict(Counter(a['source'] for a in confirmed)),
                            full_verifications=len(full), **latency_stats(latencies, 'latency'),
                            **latency_stats(full, 'full_latency'))
    counters = diagnostic.counters
    events = {k.removeprefix('event_'): v for k, v in counters.items() if k.startswith('event_')}
    queries = [e for a in dispatched for e in a['events'] if e.get('event') == 'fx_full_query']
    deferred = [e for a in dispatched for e in a['events'] if e.get('event') == 'fx_full_query_deferred']
    full = [a['query_duration'] for a in actions if a['query_duration'] is not None]
    return dict(runtime_seconds=runtime, dispatched=len(dispatched), actions=len(actions),
                 slots=slots, sources=dict(Counter(a['source'] for a in actions)),
                 **latency_stats([a['latency'] for a in actions], 'latency'),
                 full_verifications=len(full), **latency_stats(full, 'full_latency'),
                 full_query_required=sum(any(e.get('event') in ('fx_full_query', 'fx_full_query_deferred')
                                             for e in a['events']) for a in dispatched),
                 full_query_retries=sum(e.get('first') != '1' for e in queries),
                 full_query_timeouts=int(pending is not None and
                                         any(e.get('event') in ('fx_full_query', 'fx_full_query_deferred')
                                             for e in pending['events']) and
                                         diagnostic.stop is not None and 'timeout' in diagnostic.stop),
                 full_query_failures=sum(e.get('sent') != '1' for e in queries),
                 retries=sum(a['retries'] for a in dispatched), busy_deferrals=len(deferred),
                 deferred_owners={k.removeprefix('busy_owner_'): v for k, v in counters.items() if k.startswith('busy_owner_')},
                 battery_polls=counters['event_battery_poll_sent'],
                 battery_poll_sent=counters['event_battery_poll_sent'],
                 battery_poll_deferred=counters['event_battery_poll_deferred'],
                 model_conflicts=counters['model_conflicts'], chain_conflicts=counters['chain_conflicts'],
                 events=events,
                 anomalies={k: v for k, v in events.items()
                            if k in ('frame_discard', 'multipart_discard', 'preset_parse_reject',
                                     'incoming_reject', 'stream_message_reject', 'stream_discard',
                                     'multipart_timeout', 'ingress_trace_lost')},
                connections={k.removeprefix('event_').removeprefix('connection_'): v for k, v in counters.items()
                             if k in ('event_disconnect', 'event_connect', 'connection_status_lost',
                                      'connection_cli_disconnected')})


def parse_args(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port', required=True)
    mode = p.add_mutually_exclusive_group(required=True)
    mode.add_argument('--focus', action='store_true')
    mode.add_argument('--count', type=int)
    p.add_argument('--cadence', type=float, default=3)
    p.add_argument('--ready-timeout', type=float, default=60)
    p.add_argument('--action-timeout', type=float, default=20)
    args = p.parse_args(argv)
    if (args.count is not None and not 1 <= args.count <= 500) or args.cadence < 3 or args.ready_timeout <= 0 or args.action_timeout <= 0:
        p.error('count must be 1..500, cadence >= 3, timeouts > 0')
    return args


def main():
    args = parse_args()
    diagnostic = Diagnostic(args.focus, args.count or 12, args.cadence, args.ready_timeout, args.action_timeout)
    started_at = time.monotonic()
    path = f"/tmp/opencode/panelan_fx_{datetime.now().strftime('%Y%m%d_%H%M%S_%f')}.log"
    fd, original, buf = None, None, b''
    next_status = 0
    try:
        with open(path, 'xb') as log:
            fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            original = termios.tcgetattr(fd)
            cfg = termios.tcgetattr(fd)
            cfg[0] = cfg[1] = cfg[3] = 0
            cfg[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
            cfg[4] = cfg[5] = termios.B115200
            cfg[6][termios.VMIN] = cfg[6][termios.VTIME] = 0
            termios.tcsetattr(fd, termios.TCSANOW, cfg)
            while not diagnostic.stop:
                now = time.monotonic()
                diagnostic.tick(now)
                if diagnostic.stop:
                    break
                if diagnostic.current is None and not diagnostic.ready and now >= next_status:
                    os.write(fd, b'status\n')
                    next_status = now + 1
                command = diagnostic.next_command(now)
                if command:
                    os.write(fd, (command + '\n').encode())
                for probe in diagnostic.probe_commands():
                    os.write(fd, (probe + '\n').encode())
                readable, _, _ = select.select([fd], [], [], 0.1)
                if readable:
                    data = os.read(fd, 4096)
                    if not data:
                        diagnostic.fail('serial EOF')
                        break
                    buf += data
                    while b'\n' in buf:
                        raw, buf = buf.split(b'\n', 1)
                        raw += b'\n'
                        log.write(datetime.now().isoformat(timespec='milliseconds').encode() + b' ' + raw)
                        log.flush()  # raw bytes precede any decoding/normalization
                        diagnostic.on_line(raw, time.monotonic())
                        if diagnostic.stop:
                            break
    except (OSError, termios.error) as exc:
        diagnostic.fail('serial/log error: ' + str(exc))
    finally:
        if buf:
            with open(path, 'ab') as log:
                log.write(datetime.now().isoformat(timespec='milliseconds').encode() + b' ' + buf)
        if fd is not None:
            if original is not None:
                termios.tcsetattr(fd, termios.TCSANOW, original)
            os.close(fd)
    report = dict(stop=diagnostic.stop, last_good=diagnostic.actions[-1] if diagnostic.actions else None,
                  failed=diagnostic.current, models=diagnostic.models, counts=dict(diagnostic.counters),
                  dispatched=len(diagnostic.actions) + int(diagnostic.current is not None and diagnostic.current['msg'] is not None),
                   logfile=path, actions=diagnostic.actions,
                   summary=summary(diagnostic, time.monotonic() - started_at))
    print(json.dumps(report, indent=2, default=str))
    return 0 if diagnostic.stop == 'completed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
