# Spark 2 FX diagnostic (trace firmware)

Operator-only; do not run against an unverified amp or use this as a recovery
procedure. Requires `PANELAN_PRESET_TRACE` firmware and an explicit CDC port.
Opening the port may reset the controller. Check the recorded hardware attempt
below before deciding whether another action is safe.

```sh
python3 tools/diagnose_panelan_fx.py --port /dev/ttyACM0 --focus
python3 tools/diagnose_panelan_fx.py --port /dev/ttyACM0 --count 500
PYTHONPATH=tools python3 -m unittest tools/test_diagnose_panelan_fx.py
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

The first Ready block pins Spark serial, active hardware slot/current preset,
confirmed hardware preset, and preset name for the entire run. Ready also
requires no pending/failed preset action. Any subsequent complete status
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

## Read-only state re-establishment attempt (2026-10-07)

After the parser fix, the controller status gate reported Spark 2, connected,
phase Ready, and confirmed hardware slot 4. No FX command was sent. A read-only
`refresh` was accepted and full query msg 14 assembled and parsed, but the
controller emitted `preset_apply_gate expected=0 match=0 acceptedActive=0` and
`HW name cache/full preset: rejected ... checksum=44 expected=4e`; it emitted no
`Message processed` JSON from which to read `bias.noisegate.IsOn`. Thus the
previously dispatched gate action's resulting FX state was not re-established.
Capture: `/tmp/opencode/panelan_fx_readonly_20261007_204334.log`. No FX toggle,
busy probe, preset probe, focused six-slot sequence, or 500-action run followed.
Stop for review; do not send a compensating toggle or assume the prior OFF
command succeeded.
