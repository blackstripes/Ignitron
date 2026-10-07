# Spark receive framing

BLE notification fragments (including a lone `F0`) are joined into individual
`F0 01 ... F7` frames before multipart assembly. For preset responses (`01/01`
and `03/01`), multipart metadata is recognized in decoded eight-bit data only
when its count is greater than one and its index and chunk length match the
encoder's shape (25-byte nonfinal amp chunks, 128-byte nonfinal app chunks).
The assembler accepts parts in order from zero through count minus one, with
the same message number, command, subcommand and count. It publishes only
complete responses. A new part zero supersedes an unfinished response; an out-of-order,
duplicate (including a repeated start), malformed or mismatched continuation
discards the pending response.
An unrelated single-frame notification is processed immediately without
discarding a pending preset. Parser/link reset clears both fragment and
multipart state. A valid unprefixed single-frame preset publishes its entire
decoded data unchanged; the encoder does not emit a prefix for one chunk.
BLE notification boundaries are not frame boundaries: even `F0 01` at the
start of a fragment may be the previous frame's sequence/checksum bytes.
The fragment reader checks the XOR checksum of seven-bit wire data at `F7`;
if a stale incomplete frame precedes a fresh start, it resynchronizes on the
next checksum-valid `F0 01 ... F7` frame. The host regression covers both a
split `F0 01 | F0 01 ...` valid frame and recovery after stale bytes.

When one BLE block contains multiple completed logical messages, the reader
queues them in wire order. The receive consumer drains them one at a time:
each message is parsed immediately before its response/state/correlation
handling, so parser identity and message number cannot be overwritten by the
next message. Retained-intent dispatch stays after the entire block, not
between coalesced messages.

Host-only regression (no device build or flash):

```sh
g++ -std=c++17 -Wall -Wextra -Werror -Isrc tools/test_spark_receive_assembly.cpp -o /tmp/test_spark_receive_assembly && /tmp/test_spark_receive_assembly
g++ -std=c++17 -Wall -Wextra -Werror -DPANELAN_PRESET_TRACE -Isrc tools/test_spark_receive_assembly.cpp -o /tmp/test_spark_receive_trace && /tmp/test_spark_receive_trace
python3 tools/test_spark_receive_batch.py
python3 tools/test_spark_stream_structure.py
```

The batch test compiles the production reader's receive/drain methods with a
host observation hook instead of Arduino preset parsing; it checks both
preset-then-notification and notification-then-preset delivery and verifies
the consumer's per-item handling order. The structure/drain test compiles the
production `structureData`, seven-bit decoder, `readMessage`, and `nextMessage`
with a minimal host status/interpreter seam; it checks decoded data, 17-part
concatenation, sequence values including `F7` and wrap, multiple frames,
optional headers, malformed input, rejection, and subsequent queue draining.

Assembly hands the downstream reader complete checksum-valid frame vectors.
`structureData` validates each frame's minimum size, `F0 01` start and final
`F7` before decoding; it does not concatenate and re-split on raw `F7`.
Failed or empty structure results are rejected by `nextMessage`: message and
last message number are cleared, and the consumer drains the next queued item
without app-mode response handling or response-lane completion. In trace builds
`stream_message_reject` marks this downstream rejection without payload bytes.
Valid-message decoding and preset multipart concatenation are unchanged.

`PANELAN_PRESET_TRACE` is opt-in. In addition to parse rejections it reports
callback `ingress_seen` (monotonic notification ID, millisecond timestamp and
length), `ingress_enqueue`/`ingress_drop`/`ingress_drop_full`/`ingress_skip`,
`ingress_dequeue` (including original callback timestamp), `process_block`,
`frame_complete`, trace-only `frame_span_complete`, and `assembly_result`.
`frame_complete` retains its existing validated-header/count/part record;
`frame_span_complete` records the contributing ingress-ID range for that
checksum-valid wire frame. Server/app queue writes and serial
inputs use ID zero (not a BLE notification). Frame logs contain only validated
wire header identity, length, and, for identifiable preset multipart frames,
count and part. Assembly results report whether a logical message was completed
and its frame count. The ID denotes the block that produced the completed
frame/assembly result: split frames and multipart presets may have started in
earlier notifications; coalesced frames share the producing block's ID. These
logs are diagnostic only. Callback/queue trace events are copied into a
64-entry fixed-size ring without serial I/O or waiting on a trace mutex, then
printed in bounded batches from `checkForUpdates()` on the controller task.
The existing ingress queue locking and drop behavior are unchanged. If the
trace ring fills, newer trace events are omitted (not notifications); a
subsequent `ingress_trace_lost count=N` reports the saturated loss count for
that drain. Its timestamp is the drain time, not the lost events' times.
Because draining and processing run independently of callbacks, use IDs and
timestamps rather than printed line order to correlate events.
An invalidated dequeue is recorded but has no `process_block` event.

Existing framing events include
`frame_discard` (checksum, invalid wire, malformed start, overflow/recovery or incomplete reset;
checksum and invalid-wire resync are reported even when a later frame is recovered)
and `frame_span_complete` (accepted candidate provenance; `frame_span_exact=1`
means the ID and byte span is the exact completed frame), plus
`multipart_start`, `multipart_progress`, `multipart_complete`, and
`multipart_discard` (invalid frame/shape, single preset, duplicate/new start,
identity mismatch, out of order, missing start or reset). Each event carries
time, reason, message number, command/subcommand, expected and received chunk
counts; frame events use received as buffered wire bytes and expected as zero.
An `incoming_reject` event reports invalid frame or preset shape even with no
pending multipart response; a separate `multipart_discard` reports any pending
response discarded by that rejection. Incoming rejection counts are zero.
No device identity or payload bytes are logged. A pending response on parser/link
reset emits its incomplete chunk count. These observations do not change what
the receiver accepts or publishes.

For full-preset handoff, `preset_parse_complete` (message/cmd/sub, slot,
frame count and decoded length) follows successful preset parsing;
`preset_parse_reject` includes message/cmd/sub and parse/tail/checksum
diagnostics without payload. `preset_apply_gate` records the parsed message,
controller expected message and match, response-lane owner at the time of
state handling (active/message/subcommand), slot, cache acceptance,
`acceptedActive`, and observation revision before/after. A successful active
observation emits `preset_observation_publish` with message, slot and new
revision. `response_lane_complete` records the received identity and lane
state immediately **after** app state handling, before lane completion, plus
whether it matched/released the owner. Thus the apply gate may see an active
owner even when completion subsequently releases it. `controller_full_expect`
records nonzero expectation installation; `controller_full_revoke` is emitted
only when a nonzero expectation is actually cleared, with path (`expect`,
`owner_expire`, `owner_invalidate`, `link_reset`, or `accepted_observation`).
`response_owner_expire` and `response_owner_invalidate` record lane changes that
leave the controller expectation intact. These are controller-task serial
diagnostics only, not new correlation gates; message zero denotes no
expectation/active owner.

## NEO Core incomplete-response trace (2026-10-06)

On a connected Spark NEO Core, repeated normal hardware
preset selections reproduced an incomplete `03/01` full-preset response. At
`t=1832533`, message 43 (17 parts) completed part 15, with multipart progress
at 16/17. Ingress IDs 1704 and 1706 were seen, enqueued, and dequeued while
completing parts 14 and 15. The next logged notification blocks, IDs 1707 and
1708, were also seen, enqueued, and dequeued. At `t=1834181`, framing logged
`frame_discard reason=invalid_wire_resync ... received=45`, then completed the
valid part 0 for message 44. The new start discarded message 43 with
`expected=17 received=16`; no valid message-43 part 16 frame was observed.

This establishes that the receiver dequeued bytes after part 15, but the old
trace could not tie the rejected 45-byte candidate to its contributing BLE
notification IDs. It therefore did not distinguish absent callback bytes from
a truncated or invalid would-be final frame. The serial observation is
preserved here by event identity, timestamps, IDs, and lengths; no payload
bytes were recorded. Subsequent trace-only frame provenance is intended to
resolve this gap. No transport, queue, response-lane, parser, retry, or timing
behavior was changed during this reproduction.

## NEO Core provenance-trace reproduction (2026-10-06)

After flashing `panelan-lvgl-controller-preset-trace` with framing provenance,
the connected NEO Core reproduced a second 17-part stall: message 57 completed
parts 0–15, reaching 16/17. Part 15 completed as a valid 39-byte frame with
`id=654`. The later notification IDs 655 and 656 were both seen, enqueued, and
dequeued. At `t=136953`, the reader reported:

```text
event=frame_discard reason=invalid_wire_resync msg=57 cmd=03 sub=01 expected=0 received=45 first_id=654 last_id=656 partial_bytes=45 start=1 header=1 f7=1 frame_span_exact=0
event=frame_span_complete reason=none msg=58 cmd=03 sub=01 expected=0 received=39 first_id=655 last_id=656 partial_bytes=39 start=1 header=1 f7=1 frame_span_exact=1
event=frame_complete id=656 len=39 msg=58 cmd=03 sub=01 count=17 part=0
event=multipart_discard reason=new_start msg=57 cmd=03 sub=01 expected=17 received=16
```

Thus bytes from IDs 654–656 did reach the queue consumer, but no valid
message-57 part 16 was produced. The 45-byte resync buffer contains a
six-byte message-57 start/header prefix followed by the exact 39-byte valid
message-58 part-0 span (IDs 655–656). The message-57 prefix has no payload or
own `F7` before the next valid start; the logged `f7=1` is the end of the
recovered message-58 frame. This is consistent with a would-be final frame
truncated after its header, followed by the next response. It answers case 2:
bytes for a would-be final frame reached callback/queue and remained
unterminated. This conclusion uses lengths, validated headers, ID spans and
terminator placement only; no payload bytes are captured. This remains
diagnostic evidence only; transport configuration and behavior were not tuned.

## Spark 2 comparison run (2026-10-06)

For a future event-driven Spark 2 trace soak (not a hardware run), use
`python3 tools/diagnose_panelan_presets.py --port /dev/ttyACM0` with the
`panelan-lvgl-controller-preset-trace` firmware. Opening CDC may reset the
controller. The tool opens the explicit port once at 115200, polls only `status`
until a complete status block reports connected, `Amp: Spark 2`, known serial
and preset name. The CLI status now includes snapshot-backed `Controller phase`
(`Scanning`, `Reconnecting`, `Identifying`, `Syncing`, `Ready`), `identityKnown`,
`sparkStateStale`, `fullPresetObservedForLink`, `confirmedHardwarePreset`,
`pendingHardwarePreset`, and `presetActionFailed`. A complete status block can
establish startup readiness without a witnessed startup trace: phase `Ready`,
identity known, not stale, full preset observed for this link, and confirmed
hardware preset 1–8 equal to the CLI active slot. A cached name/slot alone, a
Syncing phase, false full-observed, stale state or slot mismatch cannot pass.
`status` now prints `Active hardware slot: N` (1–8 on Spark 2), calculated
from `activeHWBank() * PRESETS_PER_BANK + activePresetNum()`; the existing
`Bank: ...  Preset: ...` line reports the UI bank and within-bank preset
number (1–4), not the hardware slot. The diagnostic compares
`confirmedHardwarePreset` with the full active hardware slot, not the
within-bank number. On older status output without the explicit slot, only
an authoritative `Active hardware bank` plus a valid within-bank preset can
derive a full slot; `Bank:` and cached name/preset alone cannot, so startup
remains blocked without that evidence.
A witnessed startup `id=0` full-query/`Ready` pair with a matching active slot
remains an alternative for older trace firmware without snapshot status; a
present but not-ready snapshot cannot be overridden by that trace. The tool
sends **no preset commands** on readiness timeout or on a
different identified amp. CLI status reports the active preset number, but by
itself does not prove current-link full-preset readiness. A matching `already_current` rejection
or an action's matching `Ready` updates the tracked slot. A known preset name
alone does not prove current-link full readiness; subsequent actions require
their own trace-correlated full query and `Ready`. Do not use a mixed/incomplete
status block as readiness evidence.

It cycles slots 1–8 until 150 accepted *changing* requests (configurable with
`--count` up to 500), waiting for the current action's matching query/Ready before the
next command; a no-op moves to the next slot without counting. `--cadence` is a
minimum three-second spacing, not a timer that releases an outstanding action.
`--action-timeout` (default 15 seconds) bounds silence. A controller failure,
full timeout/retry, current-query send failure, relevant incomplete/discarded
response, parse rejection, parsed response without a published observation at
response-lane completion, identifiable frame rejection, disconnect or status
connection loss, trace-event loss, serial error/EOF or missing required trace
fields stops the run.
Superseded query messages and unrelated generic traffic are not current-query
failures. Trace-build status additionally shows `lastSubmissionStatus`
(`Sent`/`Busy`/`Failed`), `currentCommand hasRemaining` and `remainingParts`,
`responseLane active`, `owner msg`, `owner sub` (hex), and
`controllerFullPresetMessageNumber`. Inactive lane owner fields are zero. These
are read-only point-in-time diagnostics, not evidence of the owner at an earlier
failed submission. In trace builds, `full_query` and `startup_full_query` include
`lastSubmissionStatus`, `currentCommandHasRemaining` (0/1),
`currentCommandRemainingParts`, `responseLaneActive` (0/1), `ownerMsg`,
`ownerSub` (hex), and `controllerFullPresetMessageNumber`, sampled immediately
after query submission. Startup Busy submissions also emit `sent=0` before
returning to retry; the harness records and waits for that startup retry, while
an action `full_query sent=0` is terminal. Other startup send failures remain
terminal. The harness reports event-time fields directly on a terminal
`sent=0`; its latest complete status transport fields are supplementary only
and may not coincide with the submission. `Looper loops` remains the final
status line. Every raw serial line is timestamped and
flushed to `/tmp/opencode/panelan_preset_diagnostic_*.log`; the terminal summary includes
stop reason, counts, last Ready, current action/query, 50 recent trace events,
nearby raw context and log path. Host-only tests:
`PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tools -p test_diagnose_panelan_presets.py`.

The trace-only `number_query` event now includes the same seven transport
snapshot fields as `full_query`, sampled immediately after
`getCurrentPresetNum()` returns: `lastSubmissionStatus`,
`currentCommandHasRemaining`, `currentCommandRemainingParts`,
`responseLaneActive`, `ownerMsg`, `ownerSub` (hex), and
`controllerFullPresetMessageNumber`. A matching action's `number_query sent=0`
with `lastSubmissionStatus=Busy` is counted and may recover on a later poll;
other statuses or a missing submission status stop the run. A number timeout
still stops it. The terminal summary prints total number-query attempts,
`sent=0` counts grouped by submission status (including missing), and accepted
action IDs with more than one **sent** verification poll, showing each ID's
attempts/sent counts. Attempts include refused Busy polls; sent polls do not.

Hardware startup-gate attempt (2026-10-07): `/dev/ttyACM0` status reported
`Spark connected: yes`, `Amp: Spark 2`, a known serial, and current preset 3.
Neither of two 30-second startup windows observed the required correlated
startup full-query/Ready trace, so the diagnostic correctly sent zero preset
commands and did not start an action run. This is not evidence of a preset
transport failure or a Spark 2 disconnect. Captures:
`/tmp/opencode/panelan_preset_diagnostic_20261007_065013.log` (initial capture;
its chunk-timestamp formatting was subsequently corrected) and
`/tmp/opencode/panelan_preset_diagnostic_20261007_065127.log` (corrected raw
line capture). Do not infer the cause of the missing startup trace from these
captures; no current startup full-query message number or Ready action exists.
This outcome reflects the **previous trace-only startup gate**, before snapshot
status readiness was available. The subsequent post-flash attempt is recorded
below.

Post-snapshot-status diagnostic attempt (2026-10-07): the updated trace firmware
built and flashed successfully, but the 45-second startup window never reached
Spark connected/identified state. Status remained `Scanning`,
`identityKnown=false`, `sparkStateStale=true`,
`fullPresetObservedForLink=false`, and `confirmedHardwarePreset=0`. No startup
query/Ready message was observed, no preset command was sent, and zero changing
actions were accepted. Trace transport status remained `lastSubmissionStatus=Failed`,
no remaining command parts, no active response lane, and controller full-preset
message 0; this is the idle scan state, not a failed full-query submission.
Physical Spark power was not independently verified. The run was not resumed.
Capture: `/tmp/opencode/panelan_preset_diagnostic_20261007_071758.log`.
There is no preset anomaly or root-cause conclusion from this startup-only run.
Afterward active hardware-slot reporting was corrected, rebuilt and flashed; no
second diagnostic session was opened because Spark remained unavailable and
the run must not resume automatically after startup failure.

Prompt-prefixed trace parser correction and Spark 2 diagnostic (2026-10-07): an
earlier rerun reached authoritative Ready and issued action 1, but the CLI
printed its `accept` event as `> PRESET_TRACE ...`. The harness did not recognize
that first line, then timed out internally although the controller trace showed
action 1 / target 1 reaching matching `Ready` for full query msg 12 in 1194 ms.
This was a harness correlation error, not a controller/transport failure. The
parser now strips only recognized prompt markers at the beginning of parsed
lines; the raw serial line is logged before this normalization. Host tests cover
the unprefixed, single/repeated prompt, embedded-text rejection, status-block,
and exact action-1 cases.

With the parser correction, the explicit 150-action run passed the snapshot
startup gate (`Spark 2`, phase Ready, current hardware slot 1, confirmed slot 1)
and completed **150 accepted changing actions / 150 matching Ready events** in
459.35 seconds. The first slot-1 request was rejected `already_current` and was
not counted; accepted action IDs ran 2–151, with final action 151 / target 7 /
msg 226 reaching Ready in 1073 ms. Full-query-to-Ready latency from each action's
trace was p50 **1060.5 ms**, nearest-rank p95 **1135 ms**, max **2695 ms**. Each
of 150 actions had one sent full query; all 150 current full responses completed
multipart assembly (113 × 17/17, 37 × 18/18), parsed, published an observation,
and completed the response lane. There were no full-query send failures,
full-data/startup timeouts, explicit full-query retries, parse rejects,
frame/multipart discards, incoming rejects, trace-ring losses or disconnect
events.

One action (id 75 / target 3) had two `number_query sent=0` poll attempts at
elapsed 822 and 1322 ms. A later poll sent at elapsed 1907 ms and the matching
number confirmation arrived at 1916 ms, followed by the full query and Ready.
The number-query event does not include the event-time submission/owner fields,
so this trace cannot prove why those two verification polls were refused. The
action recovered and completed; no full-preset query was refused. This is
reported separately from full-query retries/timeouts and is not assigned a
root cause. Capture:
`/tmp/opencode/panelan_preset_diagnostic_20261007_080021.log`. The run stopped
at 150; the 500-action soak was not started.

500-action acceptance attempt (2026-10-07): the trace firmware built/flashed,
and authoritative Spark 2 Ready status passed the startup gate at hardware slot
7. The run stopped on its first genuine anomaly after **78 accepted changing
actions / 77 matching Ready actions**, at elapsed runtime **237.3 seconds**.
Last Ready was action 77 / target 5 / msg 244 (`elapsed=1209` ms). Current action
was id 78 / target 6; its sent full query was msg 247. No automatic reconnect or
resume occurred. The controller had reported Spark connected at startup; no
BLE `disconnect` or serial failure was observed before stopping, and physical
amp power was not independently measured.

For action 78, `number_query` sent successfully and confirmed slot 6. The
subsequent full query also sent successfully with event-time status
`lastSubmissionStatus=Sent`, `currentCommandHasRemaining=0`, remaining parts 0,
response lane active with owner msg 247 / subcommand 01. `controller_full_expect`
then installed msg 247. Receive tracing shows valid frame completions and
multipart progress through part 16, followed by `multipart_complete` 17/17 for
msg 247. However, there is **no** `preset_parse_complete`,
`preset_parse_reject`, `preset_apply_gate`, or observation publish for msg 247.
The next lane event was `response_lane_complete msg=246 cmd=00 sub=00` while the
active owner was still msg 247 / subcommand 01 (`matched=0 released=0`). The
owner then expired/revoked, followed by `full_data_timeout id=78 target=6
msg=247 elapsed=2413`. No frame discard, multipart discard, incoming reject,
trace loss, or disconnect was logged. This proves a complete assembly did not
reach the parser/publication handoff evidenced by the expected preset events,
but does **not** prove why; no firmware fix or root cause is assigned.

Before stopping, number-query attempts totaled 78, with zero `sent=0`; no action
required multiple sent verification polls. Full-query attempts totaled 78, all
sent; one full-data timeout occurred and no retry was issued because the
harness stopped immediately. The 500-action run remains incomplete. The
trace-only number-query snapshot and harness Busy accounting are included in
this commit; no controller decision, poll timing, timeout, or retry behavior
was changed. Capture:
`/tmp/opencode/panelan_preset_diagnostic_20261007_083123.log`. Stop for review;
do not continue directly to another soak.


Using the same controller, flashed `panelan-lvgl-controller-preset-trace`
image, serial CLI, 1–8 cycling order, and four-second interval between preset
requests, the controller reported `Amp: Spark 2` and remained connected. All
24 requests were accepted. Each produced a matching full-preset result and
`ready` observation; all multipart assemblies completed (17/17 or 18/18).
There were zero incomplete multipart responses, frame discards, multipart
discards, or valid partial-final-frame headers without a completed frame.
No `startup_full_timeout`, full-query send failure, or retry event was logged.

For all 24 accepted actions, `ready.elapsed` full-sync latency was (median p50,
nearest-rank p95/p99, milliseconds): p50 **1067**, p95 **1119**, p99 **1194**, max
**1194**. To match the 12 NEO Core actions that had been accepted for preset
numbers 1–4, the Spark 2 subset of those same 12 actions measured p50 **1077.5**,
p95 **1194**, p99 **1194**, max **1194**. The earlier NEO Core four-second
capture had 12 accepted requests and 12 CLI rejections (`Preset request
rejected; wait for synchronization to finish`); it contained one 17-part
incomplete response (message 57, 16/17), followed by a fresh response start.
Its output filter did not retain controller `ready` events, so NEO Core
full-sync percentiles and exact controller timeout/retry counts are unavailable
from that capture. No ingress busy/full-drop, trace-loss, or disconnect event
appeared in the NEO Core capture; interval-scoped diagnostic counters were not
collected there.

During the Spark 2 measurement window there were no disconnect events, callback
queue busy/full drops, ingress trace losses, frame discards, or multipart
discards. The follow-up since-boot diagnostic reported ingress busy=0 and
full=0 (high-water mark 12). It also reported one BLE disconnect/reconnect and
one generic transport retry/timeout since boot; those counters are not
time-scoped and cannot be attributed to this Spark 2 measurement window.
This comparison found no valid partial-final-frame header on Spark 2. A later
Spark 2 follow-up identified the separate `0xF7` sequence-number/terminator
collision described below; that is not the NEO Core final-frame-remainder
signature. The comparison does not by itself establish the amp as the cause:
the NEO Core sample had one incomplete response among 12 accepted actions, and
this Spark 2 sample had zero among 24 for that specific signature.
No transport, parser, retry, deadline, or queue behavior was changed.

## Sequence-number/terminator collision (2026-10-06)

The stopped Spark 2 soak capture (`/tmp/opencode/spark2_soak500.log`) confirms
the message-number collision. Both action 95 and action 179 issued a full-preset
query with `msg=247` (`0xF7`); each was followed by a burst of 17
`frame_discard reason=invalid_wire received=3` events. For action 95, the
first query notification was observed at `t=2325664`, and invalid three-byte
candidates were reported for ingress IDs 3677 through 3709. The surrounding
full-query progression was 244, 247, 250, 253, then 1 after message-number
wrap. Action 179 repeated query message 247 and the same 17-candidate pattern.

Root cause: `SparkReceiveFrames::accept()` treated every `0xF7` byte as a
terminator, including byte 3 (`F0 01 F7`) where it is the legal sequence byte.
That prematurely validated/rejected and cleared the partial frame before the
remainder arrived. The frame reader now only considers `F7` a possible
terminator after the minimum seven-byte wire-frame size (six-byte header plus
terminator). It still validates checksum and seven-bit payload, and retains
the existing resynchronization and provenance paths; no message number is
avoided or special-cased.

The host regression constructs valid wire frames with sequence `0xF7` and
demonstrates the old failure (`frame_discard reason=invalid_wire received=3`).
It also covers ordinary sequence numbers, `0xF0`, all split positions,
coalesced frames, a true short `F0 01 F7` prefix, `0xFF` to `0x01` sequence
wrap, and a full 17-part preset using sequence `0xF7`. Both normal and trace
host builds pass; the receive-batch and preset-stress host suites pass. Both
normal and trace PlatformIO targets build, and the trace target was flashed.

### Post-power-on Spark 2 q=0xF7 trace

Treat the operator-reported amp power-off and manual power-on as a hard
measurement boundary. The pre-boundary capture is
`/tmp/opencode/spark2_query247_action.log`; the separate post-power-on trace is
`/tmp/opencode/spark2_after_reconnect_q247_proof.log`. The Spark 2 was connected
in the post-power-on segment. The q=0xF7 full-preset response completed normal
valid wire frames for parts 0–16 and `multipart_complete expected=17 received=17`;
no three-byte invalid-wire burst occurred. However, action 203's full query
`msg=247` timed out at 2412 ms before the controller observed a matching full
result. The controller retried as message 249 and reached `Ready` only on that
retry (`startup_full_result ... msg=249 match=1 ready=1`). Thus the frame
delimiter fix is confirmed at framing/multipart assembly, but the q=0xF7
transaction did **not** produce a matching Ready on its first query. This
application-level timeout/recovery remains unresolved and is not evidence
that IDs 131 or 239 share the framing root cause.

`SparkDataControl::resetStatus()` resets `nextMessageNum` to `0x01` when the
BLE reconnect request is consumed. The post-boundary controller boot trace
started its first preset action at action ID 1; the first observed command,
number response, and full query used messages 10, 11, and 12 after startup
traffic. A USB-UART controller reset was also observed while reopening serial.
Because the filtered capture did not retain the exact BLE disconnect event,
the runtime message-number reset cannot be attributed solely to the physical
amp power cycle. Keep the pre-boundary and post-boundary captures separate.

The first complete post-boundary action trace subsequently crossed the actual
full-query `msg=247` (`0xF7`) on action 203. Ingress provenance and frame events
showed exact valid frame spans for every part 0–16, followed by
`multipart_complete expected=17 received=17`; there was no byte-three discard.
However, the matching full-preset observation did not arrive before the
controller's 2412 ms full-data timeout. It retried as message 249, and only that
response produced a matching `startup_full_result`/`ready`. Therefore the
framing fix is validated for the sequence-byte collision, but a first-query
`Ready` for `msg=247` is **not** established. This application-level retry
remains for review and must not be conflated with the separately unresolved
IDs 131 (`sent=0`) and 239 (17/17 assembly without a matching Ready).

### Full-preset handoff trace follow-up (2026-10-07)

To distinguish assembly from parser/apply/ownership handling, trace-only
`preset_parse_complete`, `preset_apply_gate`, `preset_observation_publish`,
`response_lane_complete`, and controller full-query expectation/revoke events
were added. The trace does not change message acceptance or retry policy. The
six-change focused Spark 2 run is retained at
`/tmp/opencode/spark2_preset_handoff_trace_final2.log`; it is not a 500-action
soak. Opening the serial port reset the controller. The amp was not physically
powered off during this capture.

The run reproduced an assembled and parsed response that correctly did not
become authoritative because a newer preset request had superseded its query:

- At `t=7399`, startup query `msg=8` was expected for action 1 / target 2.
- At `t=7479`, action 2 / target 3 revoked expected `msg=8` before its response
  was handled. The response later reached `multipart_complete` at `t=8473` and
  `preset_parse_complete` at `t=8483`; `preset_apply_gate` at `t=8484` showed
  `expected=0 match=0`, with active lane owner `msg=10 sub=10`. It was rejected
  (`acceptedActive=0`, revision unchanged) and did not release that other lane.
- A later current query `msg=14` reached `multipart_complete` at `t=10311` and
  parsed at `t=10322`. At `t=10323`, the gate showed `expected=14 match=1`,
  published revision 1, and accepted the active preset. The lane released at
  `t=10326`; `full_result` and `Ready` followed at `t=10328`.

This demonstrates how a complete, parseable response can be intentionally
excluded from active state: its expectation was revoked **before** parsing
because a newer action took ownership. It is not evidence of a race between
`handleAppModeResponse()` and response-lane completion; app handling precedes
lane completion. `tools/test_preset_orchestration.cpp` already covers the
matching stale-full-response rule (a new intent supersedes an old full query,
and the old reply cannot refresh state).

This mechanism explains the focused reproduction, but the older action-239
trace and post-power-on action-203/msg-247 trace predate these handoff events and
do not show whether their expectations were revoked. No root cause is assigned
to either historical failure from this reproduction. No production acceptance,
timeout, or retry code was changed. Action 131 (`full_query sent=0`) remains
separate and unresolved. The reported physical Spark 2 power-off remains a
separate unresolved observation; this controller-state trace is not evidence
that it caused or was caused by the power-off.

### Confirmed downstream F7 re-split (2026-10-07; host fix, hardware pending)

The action-78/msg-247 capture above and the post-power-on msg-247 capture
establish complete 17/17 assembly without preset parsing. Code inspection
identifies a separate downstream defect after the wire-frame fix:
`SparkStreamReader::structureData` concatenated the complete validated frames
from `SparkReceiveAssembly`, then split on every raw `F7`. Sequence 247 is
`F7` at frame offset 2, so each frame produced a three-byte candidate and
failed the minimum-length check before updating `lastMessageNum`. The old
`nextMessage` ignored `readMessage(false)` failure and reported COMPLETE;
the consumer could then attempt response-lane completion using the prior
message number and empty command/subcommand (the observed stale
`msg=246 cmd=00 sub=00`). The fix consumes complete frame vectors directly,
without treating sequence bytes as delimiters or excluding any sequence value;
failure now returns REJECT and drains later queued messages without completion.
The host structure test above exercises this handoff at production method
level. No hardware result is claimed for this change.

Focused hardware proof attempt after flashing the structure fix (2026-10-07):
the 60-second startup window remained `Scanning`, with
`Spark connected: no`, unknown amp identity, and no full-preset observation.
It sent zero preset commands, so no msg-247 proof occurred and the 500-action
run was not started. Physical Spark power was not independently measured; no
automatic reconnect or resume was attempted. Capture:
`/tmp/opencode/panelan_preset_diagnostic_20261007_151245.log`. Retry the focused
proof only after the amp/link is explicitly available. Require the first msg-247
query to show all parts and `multipart_complete`, then
`preset_parse_complete`, matching `preset_apply_gate match=1`,
`preset_observation_publish`, `response_lane_complete released=1`, and action
`Ready`, with no query retry. Do not infer success from 17/17 assembly alone.
Keep this downstream parser case distinct from the NEO Core truncated-final-frame
observation and unrelated send failures.

The focused proof was attempted again after the latest trace firmware flash
(2026-10-07 15:15): the 60-second status gate again showed
`Spark connected: no`, `Amp: (unknown)`, `identityKnown=false`, and
`fullPresetObservedForLink=false`. It sent zero preset commands; no msg-247
query or focused proof was observed. Capture:
`/tmp/opencode/panelan_preset_diagnostic_20261007_151510.log`. Physical Spark
power was not independently measured. The 500-action run was therefore not
started, and no reconnect/resume was attempted. Retry only after the Spark
connection is available; do not infer a parser result from this startup-only
capture.
