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
`frame_discard` (checksum, invalid wire, malformed start, overflow/recovery or incomplete reset;
checksum and invalid-wire resync are reported even when a later frame is recovered)
and `multipart_start`, `multipart_progress`, `multipart_complete`, and
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
