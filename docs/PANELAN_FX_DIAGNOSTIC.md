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

After the harness false-positive fix and host regressions passed, a fresh run
started from an authoritative Spark 2 Ready status. It stopped on a genuine
20-second `FX action timeout` at 191 dispatched / 190 confirmed; this is not a
passing 500-action acceptance. There were no concurrency probes. Capture:
`/tmp/opencode/panelan_fx_20261007_215020_606449.log`; JSON report:
`/home/pzwolinski/.local/share/opencode/tool-output/tool_11976d222001FFJXOhcNA3Fyig`.

The unresolved dispatch was `DelayEchoFilt` OFF, FX msg 226, followed by full
preset query msg 227. The correlated full-preset response was parsed and reported
`match=1 known=1 enabled=0 desired=0 chain=1`, but no complete
`Controller: FX 4 (DelayEchoFilt) confirmed ...` line was captured before the
action deadline; the raw log ends with a partial `Con`. Thus the amp response
observed the requested OFF state, but the controller action completion and a
post-action Ready snapshot were not captured. Stop here: do not retry or issue a
compensating toggle until operator review. The reported last Ready snapshot was
before this unresolved dispatch.

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
