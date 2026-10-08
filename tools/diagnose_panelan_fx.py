#!/usr/bin/env python3
"""Trace-firmware Spark 2 FX diagnostic. Operator supplies the CDC port; never retries an action."""
import argparse
from collections import Counter, deque
from datetime import datetime
import json
import math
import os
import re
import select
import termios
import time

from diagnose_panelan_presets import normalize_cli_line, snapshot_ready, status, trace

SLOTS = ("gate", "comp", "drive", "mod", "delay", "reverb")
SEND = re.compile(r"Controller: sending FX ([0-5]) \(([^()]+)\) (on|off)")
LEGACY_EFFECT_PREFIX = re.compile(r"^Switching (?:On|Off) effect [^\r\n]*\.\.\.(PRESET_TRACE .*)$")
MAX_READY_AGE = 1.0  # seconds; a status block must immediately precede dispatch
FX_PROOF_DELIVERY_WINDOW = 30.0  # host seconds after matching Spark evidence
FX_CONTROLLER_DEADLINE_MS = 15000  # validate firmware decision time; never extend its deadline
PIN_KEYS = ('Serial', 'Current preset', 'confirmedHardwarePreset', 'Preset name')
FX_LINE = re.compile(r'^FX (gate|comp|drive|mod|delay|reverb): known=(true|false) model=(.*?) enabled=(true|false) pending=(true|false) failed=(true|false)$')


def fx_status(lines):
    """Parse FX observations only from a complete CLI status block."""
    values = status(lines)
    if values is None:
        return None
    slots = {}
    chains = []
    for raw in lines:
        line = normalize_cli_line(raw)
        if line.startswith('FX chain: '):
            chains.append(line[len('FX chain: '):])
        match = FX_LINE.fullmatch(line)
        if line.startswith('FX ') and not line.startswith('FX chain: ') and not match:
            return values  # malformed FX observation cannot authorize dispatch
        if match:
            name, known, model, enabled, pending, failed = match.groups()
            if name in slots:
                return values  # duplicate observations cannot establish Ready
            slots[name] = dict(known=known == 'true', model=model, enabled=enabled == 'true',
                               pending=pending == 'true', failed=failed == 'true')
    if len(chains) == 1:
        values['FX chain'] = chains[0]
    values['FX slots'] = slots
    return values


def fx_ready(values):
    return (snapshot_ready(values) and values.get('pendingHardwarePreset') == '0' and
            values.get('presetActionFailed') == 'false' and
            values.get('FX chain') not in (None, '', '(unknown)') and
            all((slot := values.get('FX slots', {}).get(name)) is not None and
                slot['known'] and slot['model'] not in ('', '(unknown)') and
                not slot['pending'] and not slot['failed'] for name in SLOTS))


def fx_trace(line):
    """Parse a normal trace line or the known no-newline effect logger prefix."""
    line = normalize_cli_line(line)
    event = trace(line)
    if event is not None:
        return event
    prefixed = LEGACY_EFFECT_PREFIX.match(line)
    return trace(prefixed.group(1)) if prefixed else None


def command_write_window_clear(input_ready, input_buffered, status_block_open, status_requests_pending):
    """Do not enqueue CLI commands ahead of serial data already waiting to parse."""
    return not (input_ready or input_buffered or status_block_open or status_requests_pending)


def cli_prompt_only_buffer(raw):
    """The CLI's standalone prompt may be buffered without a terminating newline."""
    return bool(re.fullmatch(rb"\s*(?:>\s*)+", raw))


class Diagnostic:
    """Hardware-free monotonic event machine; feed complete raw serial lines in arrival order."""
    def __init__(self, focus=False, count=150, cadence=3, ready_timeout=60, action_timeout=20):
        self.focus, self.count = focus, count
        self.cadence, self.ready_timeout, self.action_timeout = cadence, ready_timeout, action_timeout
        self.started = False
        self.ready = False
        self.ready_since = None
        self.wait_since = None
        self.block_requested_at = None
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
        self.pinned_status = None
        self.baseline = None
        self.confirmed_at = None

    def fail(self, reason):
        if self.stop is None:
            self.stop = reason
            if 'model changed' in reason or 'target model' in reason:
                self.counters['model_conflicts'] += 1
            if 'chain changed' in reason:
                self.counters['chain_conflicts'] += 1

    def on_status(self, values, now, requested_at=None):
        if values is None:
            return
        # A late complete Ready block must not erase an already-expired wait.
        if self.current is None and self.stop is None:
            self.tick(now)
        if self.stop:
            return
        if self.pinned_status is not None:
            if any(key in values and values[key] != self.pinned_status[key] for key in PIN_KEYS):
                self.fail('Spark identity/active preset changed')
            if (values.get('Active hardware slot') is not None and
                    self.pinned_status['Active hardware slot'] is not None and
                    values['Active hardware slot'] != self.pinned_status['Active hardware slot']):
                self.fail('Spark active hardware slot changed')
            if 'FX chain' in values and values['FX chain'] != self.baseline['chain']:
                self.fail('FX chain changed')
            for name, slot in values.get('FX slots', {}).items():
                if slot['model'] != self.baseline['slots'][name]['model']:
                    self.fail('FX model changed: ' + name)
        self.last_status = values
        if self.started and (values.get('Spark connected') != 'yes' or values.get('Amp') != 'Spark 2'):
            self.counters['connection_status_lost'] += 1
            self.fail('Spark connection/identity lost')
        self.ready = (fx_ready(values) and requested_at is not None and
                      (self.confirmed_at is None or requested_at >= self.confirmed_at))
        if self.ready:
            if self.pinned_status is None:
                self.pinned_status = {key: values.get(key) for key in (*PIN_KEYS, 'Active hardware slot')}
                self.baseline = dict(chain=values['FX chain'], slots={name: dict(values['FX slots'][name])
                                                                     for name in SLOTS})
                self.models = {i: self.baseline['slots'][name]['model'] for i, name in enumerate(SLOTS)}
                self.states = {i: self.baseline['slots'][name]['enabled'] for i, name in enumerate(SLOTS)}
            if self.current is None and any(values['FX slots'][name]['enabled'] != self.states[i]
                                            for i, name in enumerate(SLOTS)):
                self.fail('FX status state differs from last confirmed state')
            self.started = True
            self.ready_since = now
            self.wait_since = None
        elif self.current is None:
            self.ready_since = None

    def next_command(self, now):
        if (self.stop or self.current or not self.ready or self.ready_since is None or
                now - self.ready_since > MAX_READY_AGE):
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
                            events=[], payloads=[], result=None, evidence_at={}, probes=set(), probe_sent=False,
                            probe_required=probe_required, send_seen=False)
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
        if a:
            if a['sent_at'] is None and now - a['at'] >= self.action_timeout:
                self.fail('FX dispatch timeout')
            elif (a['sent_at'] is not None and a['evidence_at'] and
                  now - min(a['evidence_at'].values()) >= FX_PROOF_DELIVERY_WINDOW):
                self.fail('FX controller proof delivery timeout')
            elif a['sent_at'] is not None and not a['evidence_at'] and now - a['sent_at'] >= self.action_timeout:
                self.fail('FX action timeout')
        fresh_ready = self.ready and self.ready_since is not None and now - self.ready_since <= MAX_READY_AGE
        if not a and self.stop is None and not fresh_ready:
            if self.wait_since is None:
                self.wait_since = (self.ready_since + MAX_READY_AGE
                                   if self.ready_since is not None else now)
            if now - self.wait_since >= self.ready_timeout:
                self.fail('authoritative Spark 2 Ready timeout')
        elif not a and fresh_ready:
            self.wait_since = None

    @staticmethod
    def matching_spark(a, source):
        return any(kind == source and enabled == a['desired'] and
                   (source == 'FX_ONOFF' or
                    (a['result'] is not None and msg == a['query_msg'] and
                     a['result'].get('msg') == msg and a['query_at'] is not None and at >= a['query_at']))
                   for kind, enabled, at, msg in a['payloads'])

    def mark_evidence(self, now):
        a = self.current
        if a is None or a['sent_at'] is None or now - a['sent_at'] >= self.action_timeout:
            return
        for source in ('FX_ONOFF', 'full preset response'):
            if source not in a['evidence_at'] and self.matching_spark(a, source):
                a['evidence_at'][source] = now

    def on_payload(self, data, now):
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
                a['payloads'].append((source, enabled, now, self.payload_msg))
        self.mark_evidence(now)

    def on_line(self, raw, now, status_requests=None):
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
                self.on_payload(data, now)
        if line == '--- Ignitron status ---':
            self.blocks = [line]
            self.block_requested_at = (status_requests.popleft()
                                       if status_requests is not None and status_requests else None)
        elif self.blocks:
            self.blocks.append(line)
            if line.startswith('Looper loops:'):
                self.on_status(fx_status(self.blocks), now, self.block_requested_at)
                self.blocks = []
                self.block_requested_at = None
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
        if kind == 'battery_poll_sent' and (a['msg'] is not None or a['send_seen']):
            self.fail('battery poll sent while FX pending')
        if kind in ('frame_discard', 'multipart_discard', 'preset_parse_reject', 'incoming_reject',
                    'stream_message_reject', 'stream_discard', 'multipart_timeout'):
            self.fail('relevant transport/parser anomaly: ' + str(e))
        if 'Controller: FX ' in line and ('cancelled:' in line or 'failed' in line):
            self.fail(line)
        sent = SEND.search(line)
        if sent:
            a['send_seen'] = True  # physical send is observed even when fx_sent was lost
            if now - a['at'] > self.action_timeout:
                self.fail('FX dispatch timeout')
                return
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
                    self.mark_evidence(now)
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
        if kind == 'fx_confirmed' and not self.stop:
            if a['sent_at'] is not None and not a['evidence_at'] and now - a['sent_at'] >= self.action_timeout:
                self.fail('FX action timeout')
                return
            proof_source = e.get('source')
            source = {'FX_ONOFF': 'FX_ONOFF', 'full_preset': 'full preset response'}.get(proof_source)
            elapsed_text = e.get('elapsed', '')
            elapsed = int(elapsed_text) if re.fullmatch(r'[0-9]+', elapsed_text) else None
            if (a['sent_at'] is None or a['msg'] is None or e.get('slot') != str(a['slot']) or
                    e.get('msg') != a['msg'] or source is None or elapsed is None or
                    elapsed >= FX_CONTROLLER_DEADLINE_MS or
                    not self.matching_spark(a, source) or source not in a['evidence_at'] or
                    (a['probe_required'] and
                      a['probes'] != {'duplicate', 'preset_cli', 'preset_trace'})):
                self.fail('uncorrelated/incomplete FX confirmation: ' + str(e))
            elif now - a['evidence_at'][source] >= FX_PROOF_DELIVERY_WINDOW:
                self.fail('FX controller proof delivery timeout')
            else:
                a['source'], a['latency'] = source, now - a['sent_at']
                a['controller_elapsed_ms'] = elapsed
                a['proof_delivery_delay'] = now - a['evidence_at'][source]
                self.models[a['slot']] = a['model']
                self.states[a['slot']] = a['desired']
                self.actions.append(a)
                self.counters[source] += 1
                self.current = None
                # Require a post-confirmation snapshot before the next FX
                # dispatch; a Ready block observed during the action is stale
                # with respect to its final Spark-owned observation.
                self.ready = False
                self.ready_since = None
                self.confirmed_at = now
                self.blocks = []  # discard an incomplete status started before confirmation
                self.block_requested_at = None
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
    dispatched = actions + ([pending] if pending and (pending['msg'] is not None or pending['send_seen']) else [])
    slots = {}
    for index, name in enumerate(SLOTS):
        confirmed = [a for a in actions if a['slot'] == index]
        latencies = [a['latency'] for a in confirmed]
        controller_elapsed = [a['controller_elapsed_ms'] for a in confirmed]
        delivery = [a['proof_delivery_delay'] for a in confirmed]
        full = [a['query_duration'] for a in confirmed if a['query_duration'] is not None]
        slots[name] = dict(dispatched=sum(a['slot'] == index for a in dispatched),
                            actions=len(confirmed), on=sum(a['desired'] for a in confirmed),
                            off=sum(not a['desired'] for a in confirmed),
                            sources=dict(Counter(a['source'] for a in confirmed)),
                            full_verifications=len(full), **latency_stats(latencies, 'latency'),
                            **latency_stats(controller_elapsed, 'controller_elapsed_ms'),
                            **latency_stats(delivery, 'proof_delivery_delay'),
                            **latency_stats(full, 'full_latency'))
    counters = diagnostic.counters
    events = {k.removeprefix('event_'): v for k, v in counters.items() if k.startswith('event_')}
    queries = [e for a in dispatched for e in a['events'] if e.get('event') == 'fx_full_query']
    deferred = [e for a in dispatched for e in a['events'] if e.get('event') == 'fx_full_query_deferred']
    full = [a['query_duration'] for a in actions if a['query_duration'] is not None]
    return dict(runtime_seconds=runtime, dispatched=len(dispatched), actions=len(actions),
                 slots=slots, sources=dict(Counter(a['source'] for a in actions)),
                 **latency_stats([a['latency'] for a in actions], 'latency'),
                 **latency_stats([a['controller_elapsed_ms'] for a in actions], 'controller_elapsed_ms'),
                 **latency_stats([a['proof_delivery_delay'] for a in actions], 'proof_delivery_delay'),
                 full_verifications=len(full), **latency_stats(full, 'full_latency'),
                 full_query_required=sum(any(e.get('event') in ('fx_full_query', 'fx_full_query_deferred')
                                             for e in a['events']) for a in dispatched),
                 full_query_retries=sum(e.get('first') != '1' for e in queries),
                  full_query_timeouts=int(pending is not None and
                                          any(e.get('event') in ('fx_full_query', 'fx_full_query_deferred')
                                              for e in pending['events']) and
                                          pending['result'] is None and
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
    if ((args.count is not None and not 1 <= args.count <= 500) or
            not all(math.isfinite(v) for v in (args.cadence, args.ready_timeout, args.action_timeout)) or
            args.cadence < 3 or args.ready_timeout <= 0 or args.action_timeout <= 0):
        p.error('count must be 1..500, cadence >= 3, timeouts > 0; values must be finite')
    return args


def drain_serial_before_tick(fd, buf, log, diagnostic, status_requests, max_reads=64):
    """Consume readable complete lines before checking deadlines; never wait for more input.

    A bounded batch prevents a continuous serial stream from holding up the loop.
    In that case deadlines are still checked, then the caller drains again
    without writing commands or waiting for new input.
    """
    saturated = False
    for _ in range(max_reads):
        if not select.select([fd], [], [], 0)[0]:
            break
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
            diagnostic.on_line(raw, time.monotonic(), status_requests)
            if diagnostic.stop:
                break
        if diagnostic.stop:
            break
    else:
        saturated = True
    if not diagnostic.stop:
        diagnostic.tick(time.monotonic())
    return buf, saturated


def main():
    args = parse_args()
    diagnostic = Diagnostic(args.focus, args.count or 12, args.cadence, args.ready_timeout, args.action_timeout)
    started_at = time.monotonic()
    path = f"/tmp/opencode/panelan_fx_{datetime.now().strftime('%Y%m%d_%H%M%S_%f')}.log"
    fd, original, buf = None, None, b''
    next_status = 0
    status_requests = deque()
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
                buf, more_to_drain = drain_serial_before_tick(fd, buf, log, diagnostic, status_requests)
                if diagnostic.stop:
                    break
                if more_to_drain:
                    continue
                now = time.monotonic()
                input_ready = bool(select.select([fd], [], [], 0)[0])
                write_window = command_write_window_clear(
                    input_ready, bool(buf) and not cli_prompt_only_buffer(buf),
                    bool(diagnostic.blocks), bool(status_requests))
                if write_window:
                    if (diagnostic.current is None and
                            (not diagnostic.ready or diagnostic.ready_since is None or
                             now - diagnostic.ready_since > MAX_READY_AGE) and now >= next_status):
                        os.write(fd, b'status\n')
                        sent_at = time.monotonic()
                        status_requests.append(sent_at)
                        next_status = sent_at + 1
                    command = diagnostic.next_command(now)
                    if command:
                        os.write(fd, (command + '\n').encode())
                    for probe in diagnostic.probe_commands():
                        os.write(fd, (probe + '\n').encode())
                select.select([fd], [], [], 0.1)
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
                   failed=diagnostic.current, models=diagnostic.models, baseline=diagnostic.baseline,
                   last_status=diagnostic.last_status, counts=dict(diagnostic.counters),
                  dispatched=len(diagnostic.actions) + int(diagnostic.current is not None and
                      (diagnostic.current['msg'] is not None or diagnostic.current['send_seen'])),
                   logfile=path, actions=diagnostic.actions,
                   summary=summary(diagnostic, time.monotonic() - started_at))
    print(json.dumps(report, indent=2, default=str))
    return 0 if diagnostic.stop == 'completed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
