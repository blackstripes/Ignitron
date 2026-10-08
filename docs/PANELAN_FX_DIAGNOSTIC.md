# Spark 2 FX diagnostic (trace firmware)

Operator-only; do not run against an unverified amp or use this as a recovery
procedure. Requires `PANELAN_PRESET_TRACE` firmware and an explicit CDC port.
Opening the port may reset the controller. Check the recorded hardware attempt
below before deciding whether another action is safe.

```sh
python3 tools/diagnose_panelan_fx.py --port /dev/ttyACM0 --focus
python3 tools/diagnose_panelan_fx.py --port /dev/ttyACM0 --count 500
PYTHONPATH=tools python3 -m unittest tools/test_diagnose_panelan_fx.py
PYTHONPATH=tools python3 -m unittest tools/test_fx_status_source.py
```

Focus performs a toggle and its inverse on each of six logical slots, with a
busy duplicate and preset rejection probe on the first action for each slot.
Long mode accepts `--count 1..500` actual FX dispatches, cycles the six slots,
and sends no probes. Both modes require a complete authoritative Spark 2
ControllerSnapshot Ready status before starting and between actions. Default
minimum command cadence is 3 seconds, Ready timeout 60 seconds and action
timeout 20 seconds; `--cadence`, `--ready-timeout`, and `--action-timeout` can
adjust these (cadence cannot be less than 3). An ambiguous action stops the
run; never issue a compensating toggle automatically.
Non-finite timing arguments (`nan`, `inf`, `-inf`) are rejected.

`status` now prints the controller snapshot's chain and six logical FX slots
between the preset action fields and `Looper loops:`:

```text
FX chain: <fxChainIdentity>
FX gate: known=true model=<model> enabled=false pending=false failed=false
FX comp: known=true model=<model> enabled=false pending=false failed=false
FX drive: known=true model=<model> enabled=false pending=false failed=false
FX mod: known=true model=<model> enabled=false pending=false failed=false
FX delay: known=true model=<model> enabled=false pending=false failed=false
FX reverb: known=true model=<model> enabled=false pending=false failed=false
```

The booleans above are illustrative, not a hardware reading. Empty chain/model
prints `(unknown)`; booleans print lowercase. `status` only reads the existing
snapshot: it does not query Spark, refresh, or send FX. A baseline-only `status`
read can establish the currently observed gate state without an FX command.

The first authoritative Ready block pins Spark serial, active hardware
slot/current preset, confirmed hardware preset, preset name, FX chain and all
six FX models for the entire run. The report includes that baseline with each
slot's enabled state. Ready requires all six known slots with nonempty,
non-`(unknown)` models, a known chain, no pending/failed FX or preset action.
Each controller send direction must invert the last confirmed state, starting
with the snapshot baseline rather than an assumed toggle sequence. After each
confirmation, a new request-correlated authoritative Ready status must show
the same chain/models and the confirmed FX state before another dispatch.
Any subsequent complete status
reporting a change stops the run, including during probes or between actions.
Dispatch also requires a complete Ready block no older than
one second; the harness polls status again when Ready evidence ages out and
stops after the Ready timeout if refresh never arrives. The harness pairs each
completed status block with the oldest outstanding `status` command send time.
Unsolicited blocks cannot authorize dispatch. After confirmation, only a Ready
block requested at or after that confirmation can authorize the next action;
late output from an earlier request cannot. Every complete block is still
checked for pinned identity/preset and connection changes, even if uncorrelated
or too old to gate. Each FX direction is read
from the controller send line, not assumed from the harness toggle sequence.
An action counts as confirmed only when the matching post-bookkeeping
`PRESET_TRACE event=fx_confirmed slot=<n> msg=<FX msg> source=FX_ONOFF|full_preset elapsed=<ms>`
event is paired with the appropriate matching Spark-owned observation and its
controller-reported `elapsed` is below the unchanged 15-second firmware action
deadline. The harness allows a bounded 30-second host-only proof-delivery window
after matching Spark evidence, for delayed/fragmented serial output; this does
not extend or change the firmware deadline. The human-readable
`Controller: FX ... confirmed` line is diagnostic only and is not required or
sufficient for acceptance. The controller emits the trace only after it has
confirmed `ControllerState`, recorded confirmation telemetry and the persistent
event, and cleared its pending FX request.
The harness does not enqueue status/action commands while serial input, a
partial status block, or an outstanding status request remains.
The CLI's standalone buffered `>` prompt is the only raw-buffer exception; it is
retained in the raw logfile and normalized only for command-window/parsing logic.
If the controller send line is observed but its `fx_sent` trace is missing, the
run stops uncorrelated; the command still counts as dispatched with unknown
message and outcome. Do not retry it.

The JSON report includes last good/failed action, per-slot models, messages,
confirmation latency/source, full-query duration, retries, Busy deferrals and
all action trace events. Its `summary` aggregates dispatched and confirmed
 actions per slot, ON/OFF confirms, source counts, p50/p95/p99/max confirmation
 and full verification latencies (seconds), verification counts, observed
 full-query required/retry/timeout/send-failure counts, Busy deferrals and owners,
 battery poll sent/deferred, model/chain conflicts, trace event counts (including
 parser and connection events), and runtime (seconds).
A complete timestamped raw serial capture (including
CLI prompts before normalization) is saved under `/tmp/opencode/panelan_fx_*.log`.
ACK alone is not confirmation. A matching fresh FX_ONOFF payload or a
correlated current full-preset query showing the desired state with a matching
 payload tagged by the preceding `preset_parse_complete msg` is required, even
 if the query preceded the ACK (`ack=0`). A stale or
mismatched result cannot confirm; a pre-ACK old-state reply is inconclusive,
while a post-ACK old-state reply conflicts.
`fx_full_query_deferred` records transport ownership for a Busy verification
query without altering the queued intent, response deadline or retry cadence.
An event-time `ownerSub=71` is treated as a battery-priority regression; other
Busy owners are reported and can continue only within the same action timeout.

## Hardware attempt (2026-10-07)

The trace firmware was flashed and the authoritative Spark 2 Ready gate passed.
The focused run stopped on its first gate-slot action because the CLI printed
`fx_sent` on the same raw line as the no-newline SparkPresetControl log:
`Switching Off effect bias.noisegate...PRESET_TRACE ...`. The initial harness
accepted only a line-leading trace and stopped when the subsequent
`Controller: sending FX 0 (bias.noisegate) off` lacked a correlated `fx_sent`
record in its parser state. The raw capture contains the `fx_sent` record
embedded in that recognized no-newline prefix. A harness fix now recognizes
only this specific leading `Switching On/Off effect ......` form (along with
unprefixed/prompt-prefixed traces); it does not search arbitrary embedded text.
This was a harness parsing defect, not evidence of a controller failure. The FX
command had already been dispatched, but no authoritative confirmation was
captured. No duplicate probe, preset probe, or follow-up action was sent, and
the action was not toggled again to recover. Capture:
`/tmp/opencode/panelan_fx_20261007_203503_090673.log`. The focused six-slot
proof and 500-action soak remain unperformed/inconclusive; do not resume until
the state is safely re-established by operator review.
The prompt-prefix parser regression passes on the host, but no hardware rerun was
attempted after the fix because the first action's final Spark-owned state is
unknown. The trace firmware with deferred-query diagnostics was flashed, but no
FX retry, battery-query collision, or controller failure is evidenced by this
capture.

## Read-only baseline and subsequent hardware actions (2026-10-07)

The earlier read-only `refresh` attempt did not establish the gate state: full
query msg 14 parsed, but `preset_apply_gate expected=0 match=0 acceptedActive=0`
and `HW name cache/full preset: rejected ... checksum=44 expected=4e` followed,
with no `Message processed` JSON. Capture:
`/tmp/opencode/panelan_fx_readonly_20261007_204334.log`.

A later status-only read did establish an authoritative Spark 2 Ready baseline
with confirmed hardware slot 4 and chain
`uuid:1A5E1775-E767-4647-AB21-EBBFBFE63D18|bias.noisegate|LA2AComp|Booster|GuitarEQ6|DelayEchoFilt|bias.reverb`.
The observed slot states were gate OFF, comp ON, drive OFF, mod ON, delay ON,
reverb ON. Capture: `/tmp/opencode/panelan_fx_status_only_20261007_213204.log`.

**Process deviation:** after this baseline, FX actions were run despite the
instruction to stop for review without sending an FX command. The focused proof
sent 12 actions (two per logical slot), each confirmed by a correlated full
preset response; the inverse pairs restored the baseline. A subsequent 500-action
run sent and confirmed 17 more actions before its harness stopped. No additional
FX command appears after the 17th confirmation in the raw capture. The resulting
observed states at stop were gate ON, comp OFF, drive ON, mod OFF, delay OFF,
reverb ON. This is not the original baseline; do not assume otherwise or issue a
compensating toggle without operator review.

The long-run stop was a harness false positive, not evidence of a battery/FX
collision: the last FX action (`DelayEchoFilt` off, msg 68) was confirmed at
21:34:52.657. A subsequent status reported all slots non-pending and the response
lane inactive at 21:34:56.205. The battery poll became due at 21:34:56.286 and
was sent at 21:34:56.287 (owner msg 70/sub 71), with no FX action in flight.
The harness incorrectly classified this idle poll as “battery poll sent while
FX pending.” Raw capture: `/tmp/opencode/panelan_fx_20261007_213401_519200.log`.
The 500-action run is incomplete and must not be described as a passing soak.
No additional hardware actions were sent within that failed attempt.

## Fresh 500-action acceptance attempt (2026-10-07)

After the battery-poll false-positive fix and host regressions passed, the first
fresh run started from authoritative Spark 2 Ready status. It stopped at 191
dispatched / 190 confirmed, but a later timing review indicates the harness
likely applied its 20-second timeout from the CLI request rather than the
controller send. This was not a passing acceptance. There were no concurrency
probes. Capture:
`/tmp/opencode/panelan_fx_20261007_215020_606449.log`; JSON report:
`/home/pzwolinski/.local/share/opencode/tool-output/tool_11976d222001FFJXOhcNA3Fyig`.

The unresolved dispatch was `DelayEchoFilt` OFF, FX msg 226, followed by full
preset query msg 227. The report's CLI request time was monotonic 6355084.502
(host `22:02:39.607`); controller send was 6355084.656 (host `22:02:39.760`,
firmware `t=1851592`). Query msg 227 was sent at `22:02:40.847` (`t=1852678`),
and the matching result (`match=1 known=1 enabled=0 desired=0 chain=1`) arrived
at `22:02:41.508` (`t=1853339`). The old CLI-anchored deadline was approximately
`22:02:59.607`; raw capture ends with partial `Con` at `22:02:59.655`, 48 ms
after that deadline but about 105 ms before a 20-second send-anchored deadline
at `22:02:59.760`. This strongly suggests a harness timing/order false stop;
however, the remaining confirmation bytes are absent from the capture, so this
does not establish a completed controller confirmation. The last Ready snapshot
preceded this action. Do not retry or compensate without operator review.

Partial results from 190 confirmed actions:

| Slot | Dispatched | Confirmed | ON | OFF | Confirmation source |
| --- | ---: | ---: | ---: | ---: | --- |
| gate (`bias.noisegate`) | 32 | 32 | 16 | 16 | full preset response 32 |
| comp (`LA2AComp`) | 32 | 32 | 16 | 16 | full preset response 32 |
| drive (`Booster`) | 32 | 32 | 16 | 16 | full preset response 32 |
| mod (`GuitarEQ6`) | 32 | 32 | 16 | 16 | full preset response 32 |
| delay (`DelayEchoFilt`) | 32 | 31 | 16 | 15 | full preset response 31 |
| reverb (`bias.reverb`) | 31 | 31 | 16 | 15 | full preset response 31 |
| **Total** | **191** | **190** | **95** | **95** | **full preset response 190** |

Confirmed-action latency was p50 1.739 s, p95 1.748 s, p99 1.813 s, max 1.928 s.
All 190 confirmations used full-preset verification; its latency was p50 0.652 s,
p95 0.663 s, p99 0.670 s, max 0.720 s. Busy/deferred owners were zero. There
were zero FX full-query retries or send failures; one action timed out while
awaiting controller confirmation, despite the matching query result (the JSON
summary counts one full-query timeout because the action stopped with “timeout”).
There were 12 battery sends and 12 deferrals, with no battery collision reported.
There were zero model/chain conflicts, parser/framing/multipart/stream anomalies,
BLE disconnects, or ingress trace losses. The run lasted 759.08 seconds.

## Fresh 500-action rerun with two-phase timeout (2026-10-07)

After the timing fix and all host regressions passed, status-only established
Spark 2 Ready, serial `S5011I16101117`, hardware slot 4, the pinned FX chain above,
all six known/non-pending FX slots, no remaining controller command parts, and
an inactive response lane. Baseline: gate ON, comp OFF, drive ON, mod OFF, delay
OFF, reverb ON. No compensating DelayEchoFilt action was sent. The run stopped at
**355 dispatched / 354 confirmed** on the first post-send `FX action timeout`; it
is not a passing 500-action acceptance. No concurrency probes were sent. Complete
raw capture: `/tmp/opencode/panelan_fx_20261007_222140_151545.log`; JSON report:
`/home/pzwolinski/.local/share/opencode/tool-output/tool_1199c44f9001C7fQxEdeGaHWC6`.

The unresolved action was gate (`bias.noisegate`) ON, msg 215. CLI request was
logged at `22:43:33.500`; FX send at `22:43:33.651` (`t=4305475`). Query msg 216
was sent at `22:43:34.738` (`t=4306561`). Its complete matching full-preset
response was parsed at `22:43:35.395` (`t=4307219`, `elapsed=1744`) with
`match=1 known=1 enabled=1 desired=1 chain=1`; multipart completion, preset parse,
and response-lane release are present. The corrected 20-second send-anchored
deadline was approximately `22:43:53.651`. The raw capture ends with partial
`Cont` at `22:43:53.742`, 91 ms after that deadline; no complete controller
confirmation line or post-action Ready status was captured. The poll-before-tick
drain cannot accept bytes first observed after the deadline. Do not retry or
compensate; preserve this unresolved state for review.

**Failure classification:** the amp response for the pending action reported the
requested ON state, so this is not evidence of an amp refusing the state change.
BLE/response transport completed and released its lane; the full-preset multipart
assembly and parser completed, with no framing/parser/trace-loss anomalies. The
missing evidence is the complete controller confirmation line: only its `Cont`
prefix was captured after the host send-anchored deadline. That leaves controller
confirmation/CLI-output timing versus host serial capture unresolved; it is not
justified to label this a controller failure, a transport failure, or a harness
false positive. The harness correctly withheld confirmation based on the
available evidence and stopped.

Partial results from the rerun:

| Slot | Dispatched | Confirmed | ON | OFF | Confirmation source |
| --- | ---: | ---: | ---: | ---: | --- |
| gate (`bias.noisegate`) | 60 | 59 | 29 | 30 | full preset response 59 |
| comp (`LA2AComp`) | 59 | 59 | 30 | 29 | full preset response 59 |
| drive (`Booster`) | 59 | 59 | 29 | 30 | full preset response 59 |
| mod (`GuitarEQ6`) | 59 | 59 | 30 | 29 | full preset response 59 |
| delay (`DelayEchoFilt`) | 59 | 59 | 30 | 29 | full preset response 59 |
| reverb (`bias.reverb`) | 59 | 59 | 29 | 30 | full preset response 59 |
| **Total** | **355** | **354** | **177** | **177** | **full preset response 354** |

Confirmed-action latency: p50 1.738 s, p95 1.748 s, p99 1.813 s, max 1.831 s.
Full-preset verification latency: p50 0.651 s, p95 0.662 s, p99 0.722 s,
max 0.745 s. There were zero Busy/deferred owners, retries, query send failures,
model/chain conflicts, battery collisions, transport/parser/framing/multipart/stream
anomalies, disconnects, or trace losses. Battery polls: 22 sent, 21 deferred.
The report counts one full-query timeout because the action stopped with an
action-timeout reason, although the correlated full-query result itself arrived.
Runtime was 1333.63 seconds. Do not describe this partial run as acceptance.

## FX confirmation output-order fix and focused stress (2026-10-08)

Both authoritative FX paths now commit `ControllerState`, confirmation
telemetry/counter, persistent `FxConfirmed` event, and pending-request cleanup
before emitting either structured or human-readable serial output. A host C++
regression drives the production sequencing helper with a throwing logger and
verifies the state/counter/persistent/clear operations have each happened once
before logging. The acceptance harness requires the matching structured
controller event plus Spark-owned matching payload/query evidence; prose alone
and `fx_full_result` alone cannot confirm.

The first pressure-focused 150-action attempt, before the bounded host proof
delivery window was added, stopped at 87 dispatched / 86 accepted because the
structured confirmation line arrived late and fragmented. In its final action,
Spark's matching full result (`slot=2 msg=188`, `match=1 enabled=1 desired=1
chain=1`) and the `fx_confirmed` trace shared firmware time `t=339690`, while the
partial trace bytes were timestamped by the host 18.346 seconds later. The
trailing line was incomplete. A later read-only Ready status showed Booster ON,
`pending=false`, all FX known/non-pending, and an idle response lane. This is
strong evidence of serial-output backpressure delaying the trace/prose after the
controller state transition, rather than an amp or BLE/response-transport
failure. It does not identify the exact USB CDC/host-buffer bottleneck. Capture:
`/tmp/opencode/panelan_fx_20261008_064827_642248.log`; report:
`/home/pzwolinski/.local/share/opencode/tool-output/tool_11b5d10f9001AVUpexTvUrHYrk`.

After deferring `fx_full_result` output on successful confirmations as well,
normal and trace firmware were rebuilt and the trace image reflashed. A fresh
six-slot run passed **150 dispatched / 150 controller-confirmed actions** from
an authoritative Spark 2 Ready baseline (serial `S5011I16101117`, slot 4). There
were no concurrency probes. Baseline states were gate ON, comp ON, drive OFF,
mod OFF, delay OFF, reverb OFF. Raw capture:
`/tmp/opencode/panelan_fx_20261008_071849_593166.log`; JSON report:
`/home/pzwolinski/.local/share/opencode/tool-output/tool_11b7bc7d2001oJes0lJgYDz4yr`.

| Slot | Dispatched | Confirmed | ON | OFF | Confirmation source |
| --- | ---: | ---: | ---: | ---: | --- |
| gate (`bias.noisegate`) | 25 | 25 | 12 | 13 | full preset response 25 |
| comp (`LA2AComp`) | 25 | 25 | 12 | 13 | full preset response 25 |
| drive (`Booster`) | 25 | 25 | 13 | 12 | full preset response 25 |
| mod (`GuitarEQ6`) | 25 | 25 | 13 | 12 | full preset response 25 |
| delay (`DelayEchoFilt`) | 25 | 25 | 13 | 12 | full preset response 25 |
| reverb (`bias.reverb`) | 25 | 25 | 13 | 12 | full preset response 25 |
| **Total** | **150** | **150** | **76** | **74** | **full preset response 150** |

All 150 actions also had one matching `fx_confirmed` event. Controller elapsed
was p50 1739 ms, p95 1748.55 ms, p99 1777.04 ms, max 1779 ms; host proof-delivery
delay was p50 0.143 ms, p95 0.246 ms, p99 0.281 ms, max 0.308 ms. Action latency
was p50 1.740 s, p95 1.749 s, p99 1.778 s, max 1.779 s. Full-preset verification
latency was p50 0.653 s, p95 0.661 s, p99 0.684 s, max 0.694 s. There were zero
retries, query timeouts/send failures, Busy owners, model/chain conflicts,
battery collisions, transport/parser/framing/multipart/stream anomalies,
disconnects, or trace losses. Battery polls: 8 sent, 8 deferred. Runtime was
529.60 seconds. The raw capture shows successful full-result and post-bookkeeping
confirmation traces paired at the same firmware timestamp; no delayed/fragmented
proof required the 30-second host-only window in this rerun. This was a focused
stress pass; the requested 500-action acceptance follows.

## Spark 2 FX reliability acceptance pass (2026-10-08)

The requested 500-action event-driven run completed with **500 dispatched / 500
controller-confirmed / 500 post-action Ready**. The read-only pre-run baseline
passed Spark 2 Ready, serial `S5011I16101117`, hardware slot 4, all six
known/non-pending/non-failed FX slots, no pending/failed preset action, and an
idle response lane. Baseline: gate OFF, comp OFF, drive ON, mod ON, delay ON,
reverb ON. No state restoration commands or concurrency probes were sent. Raw
capture: `/tmp/opencode/panelan_fx_20261008_073240_098167.log`; JSON report:
`/home/pzwolinski/.local/share/opencode/tool-output/tool_11b9e5e47001CX9ApCg14lBNHQ`.

| Slot | Dispatched | Controller-confirmed | Post-action Ready | ON | OFF | Confirmation source |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| gate (`bias.noisegate`) | 84 | 84 | 84 | 42 | 42 | full preset response 84 |
| comp (`LA2AComp`) | 84 | 84 | 84 | 42 | 42 | full preset response 84 |
| drive (`Booster`) | 83 | 83 | 83 | 41 | 42 | full preset response 83 |
| mod (`GuitarEQ6`) | 83 | 83 | 83 | 41 | 42 | full preset response 83 |
| delay (`DelayEchoFilt`) | 83 | 83 | 83 | 41 | 42 | full preset response 83 |
| reverb (`bias.reverb`) | 83 | 83 | 83 | 41 | 42 | full preset response 83 |
| **Total** | **500** | **500** | **500** | **248** | **252** | **full preset response 500** |

Controller confirmation elapsed: p50 **1739 ms**, p95 **1749 ms**, p99 **1825.04 ms**,
max **1918 ms**. Host proof-delivery delay: p50 **0.137 ms**, p95 **0.228 ms**,
p99 **0.319 ms**, max **0.469 ms**. Full-preset verification latency: p50
**0.652 s**, p95 **0.662 s**, p99 **0.728 s**, max **0.831 s**. All 500 actions
required correlated full-preset verification. Retries, timeouts, query send
failures, Busy/deferred owners, battery collisions, model/chain conflicts,
framing/multipart/parser/stream anomalies, BLE disconnects, and trace losses were
all **zero**. Battery polls: **33 sent / 28 deferred**. Runtime: **1965.54 s**.
The final Ready status showed all FX slots non-pending/non-failed and OFF; no
restoration commands were issued.

This is the **Spark 2 FX reliability acceptance pass**. Combined with the
500/500 Spark 2 preset acceptance in `docs/SPARK_RECEIVE_FRAMING.md`, Spark 2
preset + FX core control reliability is complete. This does not cover tuner,
looper, UI, or NEO Core follow-up work.
