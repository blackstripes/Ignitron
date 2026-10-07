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
```

The batch test compiles the production reader's receive/drain methods with a
host observation hook instead of Arduino preset parsing; it checks both
preset-then-notification and notification-then-preset delivery and verifies
the consumer's per-item handling order.

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
