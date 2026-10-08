# Spark FX direct-confirmation investigation

## Scope and confidence labels

This investigation asks what Spark 2 actually sends after an FX change and
compares available client implementations. **Proven** means directly visible in
the preserved raw trace or cited source; it does not promote an external
project's parser behavior into verified amp behavior. **Plausible** means the
evidence supports a hypothesis but does not isolate it. **Speculative** means
there is not enough evidence to conclude. No production FX semantics or BLE
settings were changed for this investigation.

## Spark 2 wire evidence

The preserved 500-action acceptance capture
`/tmp/opencode/panelan_fx_20261008_073240_098167.log` was analyzed from incoming
`frame_complete` events, not from controller confirmation sources:

- **500/500 FX sends had one incoming CMD `04` / SUB_CMD `15` frame-complete
  event**, each carrying the same protocol message number as its FX command.
- **Zero CMD `03` / SUB_CMD `15` frames** appeared anywhere in the trace, including
  identifiable frame spans, discards, or incoming rejects. There were no frame
  discards or incoming rejects in the run, so no `03/15` reached the traced
  framing/parser path and was rejected.
- Host timestamp correlation from the `fx_sent` line to the same-message
  `frame_complete cmd=04 sub=15` gives p50 **6 ms**, p95 **29 ms**, p99 **104 ms**,
  max **252 ms**. The capture timestamps have 1 ms resolution. This is strong
  evidence that this Spark 2 sent generic `04/15` ACKs and no rich `03/15` effect
  notifications in that run; it is independent of the fact that all 500 state
  changes were later verified by full-preset responses.

A subsequent live 12-action run used ordinary count mode (no concurrency probes)
and the existing `frame_complete` trace. It likewise observed **12/12 same-message
`04/15` ACKs and zero `03/15` frames**, with no framing/parser rejects. Each action
also received a correlated full-preset verification. Read-only baseline was Spark
2 Ready, serial `S5011I16101117`, hardware slot 4, all FX known/non-pending, and
response lane idle; all six were OFF. Per-action observations:

| # | Effect | Requested state | FX msg | `04/15` | ACK latency | `03/15` | Full-query msg | Full-result latency |
| ---: | --- | --- | ---: | --- | ---: | --- | ---: | ---: |
| 1 | `bias.noisegate` | ON | 128 | yes | 110.2 ms | no | 129 | 645.1 ms |
| 2 | `LA2AComp` | ON | 130 | yes | 6.6 ms | no | 131 | 630.3 ms |
| 3 | `Booster` | ON | 132 | yes | 4.8 ms | no | 133 | 662.1 ms |
| 4 | `GuitarEQ6` | ON | 134 | yes | 6.5 ms | no | 135 | 621.0 ms |
| 5 | `DelayEchoFilt` | ON | 136 | yes | 2.6 ms | no | 137 | 659.2 ms |
| 6 | `bias.reverb` | ON | 138 | yes | 6.3 ms | no | 139 | 645.2 ms |
| 7 | `bias.noisegate` | OFF | 140 | yes | 6.3 ms | no | 141 | 707.2 ms |
| 8 | `LA2AComp` | OFF | 142 | yes | 4.8 ms | no | 143 | 622.3 ms |
| 9 | `Booster` | OFF | 144 | yes | 6.4 ms | no | 145 | 624.2 ms |
| 10 | `GuitarEQ6` | OFF | 146 | yes | 6.3 ms | no | 147 | 623.0 ms |
| 11 | `DelayEchoFilt` | OFF | 149 | yes | 6.4 ms | no | 150 | 631.9 ms |
| 12 | `bias.reverb` | OFF | 151 | yes | 6.4 ms | no | 152 | 625.4 ms |

The 12-action raw capture is
`/tmp/opencode/panelan_fx_20261008_130141_571614.log`; its JSON report is
`/home/pzwolinski/.local/share/opencode/tool-output/tool_11cae2542001BTXTRPdsX63XEL`.
The sequence toggled each slot ON then OFF; the run completed with 12/12
controller confirmations and post-action Ready snapshots, with no anomaly. No new
firmware instrumentation was needed: Ignitron's existing validated
`frame_complete` events record the incoming `cmd`, `sub`, and `msg`; identifiable
discard/reject events also retain their header identity.

## Ignitron BLE and protocol path (source-proven)

Sources: `src/SparkBTControl.h`, `src/SparkBTControl.cpp`, `src/SparkMessage.cpp`,
`src/SparkMessageSequence.h`, `src/SparkDataControl.cpp`, and
`src/SparkStreamReader.cpp`.

- **BLE stack:** h2zero NimBLE-Arduino, pinned to 1.4.3 in `platformio.ini`.
- **GATT:** service FFC0, write characteristic FFC1, notification characteristic
  FFC2. Ignitron finds FFC2, checks `canNotify()`, writes CCCD 0x2902 with
  response, then subscribes to notifications.
- **MTU:** the code calls `client_->getMTU()` but does not explicitly request an
  MTU. The negotiated runtime value is not recorded by the cited code.
- **Connection parameters:** `setConnectionParams(18, 30, 0, 600)` (interval
  units conventionally 1.25 ms, latency 0, supervision-timeout units 10 ms);
  this is the configured request, not a measurement of the negotiated link.
- **Write type:** `SparkDataControl::triggerCommand()` calls `writeBLE()` with
  `response=false`; the GATT write is therefore requested without response.
- **Chunking:** protocol data is split into Spark chunks (up to 0x80 bytes toward
  Spark), packed 7-bit, checksum protected, and wrapped in F0 01 ... F7 frames.
  The block carries the 01 FE / 53 FE envelope. `writeBLE()` separately caps GATT
  writes at 100 bytes. These are distinct protocol and ATT-write chunk layers.
- **Sequence:** normal Spark message numbers start at 1; the sequence helper
  wraps and skips reserved 0xEE. Received ACKs are tracked by message number.
- **FX response handling:** Spark CMD `04/15` is retained as a matching generic
  ACK/verification milestone; it does not confirm the FX state. Spark CMD `03/15`
  is parsed by `SparkStreamReader::readEffectOnOff()` as an effect/state
  observation. A correlated full-preset result is the fallback when direct
  `03/15` state is absent.

## Comparison projects

The source references below are pinned to the repository heads inspected for
this investigation. Parser support and comments are not treated as proof of
Spark 2 over-the-air behavior unless identified as a raw hardware observation.

### `lucanenni/spark-go-utils`

Sources: [desktop Bleak backend](https://github.com/lucanenni/spark-go-utils/blob/bc631e2ff8485b2bc1fa24a47b611ff407706ada/desktop/ble_backend.py),
[desktop protocol](https://github.com/lucanenni/spark-go-utils/blob/bc631e2ff8485b2bc1fa24a47b611ff407706ada/desktop/protocol.py),
[ESP32-S3 NimBLE backend](https://github.com/lucanenni/spark-go-utils/blob/bc631e2ff8485b2bc1fa24a47b611ff407706ada/ESP32-S3-ChocolatePlus-bridge/src/spark_ble.cpp),
[ESP32 protocol](https://github.com/lucanenni/spark-go-utils/blob/bc631e2ff8485b2bc1fa24a47b611ff407706ada/ESP32-S3-ChocolatePlus-bridge/src/spark_protocol.cpp),
[PlatformIO configuration](https://github.com/lucanenni/spark-go-utils/blob/bc631e2ff8485b2bc1fa24a47b611ff407706ada/ESP32-S3-ChocolatePlus-bridge/platformio.ini).

| Property | Desktop Bleak | ESP32-S3 path |
| --- | --- | --- |
| BLE stack | **Proven:** Bleak (`BleakClient`/`BleakScanner`). | **Proven:** NimBLE-Arduino, dependency `^1.4.1`. |
| GATT identifiers | **Proven:** numeric handles 0x0007 notify / 0x000A write in its protocol constants. | **Proven:** FFC0 service, FFC1 write, FFC2 notify. |
| MTU | **Proven:** no explicit MTU request in the reviewed backend. | **Proven:** no explicit MTU request in the reviewed client. |
| Subscription | **Proven:** `start_notify(0x0007, ...)`; OS/Bleak performs CCCD handling. | **Proven:** discovers CCCD and uses `subscribe(true, callback, true)` (CCCD write with response). Source comments report no notifications with a no-response CCCD write on the author's Spark GO. |
| Write type | **Proven:** `write_gatt_char(..., response=False)`. | **Proven:** `writeValue(..., false)`. |
| Connection parameters | **Proven:** no explicit interval/latency/supervision request in the backend; OS managed. | **Proven:** no explicit connection-parameter request found; code sets an application connect timeout, not a link interval. |
| Framing / sequence | **Proven:** notification stream reassembly, F7 splitting, 7-bit packing, sequence starts at 0x20 and wraps modulo 256, multipart preset reassembly. | **Proven:** stream reassembly/F7 splitting and multipart preset handling; explicit sequence counter. |
| `03/15` / `04/15` | **Proven source fact:** parser handles `03/15`; no `04/15` effect-ACK branch found. This is parser coverage, not a measured absence on amp. | **Proven source fact:** parser handles both. **Author-reported hardware observation:** comments say a Spark GO effect-toggle test received only generic `04/15`, not rich `03/15`. This is not Spark 2 data. |

The ESP32 path's sequence counter is explicit in source. Its normal outgoing
sequence starts at `0x01`, wraps before `0x3f`, and has a separate `0x40`-range
counter for the opposite direction; selected response-shaped packets reuse the
received sequence. **Proven source behavior; not a measured Spark 2 difference.**

### `paulhamsh/Spark`

Sources at commit `6b4b91a4a7b276e9f62cb7847cc380121dc2ff8b`: [Python communications](https://github.com/paulhamsh/Spark/blob/6b4b91a4a7b276e9f62cb7847cc380121dc2ff8b/SparkClass/SparkCommsClass.py),
[Python reader](https://github.com/paulhamsh/Spark/blob/6b4b91a4a7b276e9f62cb7847cc380121dc2ff8b/SparkClass/SparkReaderClass.py),
[ESP32 GATT communications](https://github.com/paulhamsh/Spark/blob/6b4b91a4a7b276e9f62cb7847cc380121dc2ff8b/SparkESP32/SparkComms.ino),
[ESP32 declarations](https://github.com/paulhamsh/Spark/blob/6b4b91a4a7b276e9f62cb7847cc380121dc2ff8b/SparkESP32/SparkComms.h).

- **Proven:** the Python backend uses Bluetooth RFCOMM sockets, not BLE GATT;
  GATT MTU/subscription/connection parameters therefore do not apply to that
  path. It splits application protocol payloads into up-to-0x80-byte chunks and
  handles protocol sequence/ACK fields. Its communications code extracts the
  message sequence and builds an ACK echoing that sequence under its
  direction/CMD/SUB_CMD predicate. This is protocol-level ACK generation, not
  evidence about a Spark 2 effect response.
- **Proven:** the ESP32 code has conditional Arduino BLE/NimBLE paths and FFC0,
  FFC1, FFC2 UUIDs. A `setMTU(517)` call is inside its `CLASSIC` branch; do not
  attribute that setting to the NimBLE path. The inspected code does not request
  explicit connection parameters. Its Spark write uses `response=false`. The
  ESP32 bridge has separate direction sequence counters and reuses the received
  sequence for selected response-shaped packets. Its NimBLE branch subscribes
  with `subscribe(true, callback, true)`; the `CLASSIC` branch registers the
  callback and explicitly writes the CCCD with response.
- **Proven source fact:** the Python reader labels CMD `04` as acknowledgement;
  its listed CMD `03` branches do not include `03/15`. The ESP32 data-control
  layer has `04/15` ACK references, separate from decoded CMD `03/15` handling.
  **Speculative:** this does not establish which response a Spark 2 actually
  sends to this client.

### `richtamblyn/PGSparkLite`

Sources at commit `0578a7302ad19fa53665d0fbedfc72c33d4c5c25`:
[communications](https://github.com/richtamblyn/PGSparkLite/blob/0578a7302ad19fa53665d0fbedfc72c33d4c5c25/lib/external/SparkCommsClass.py),
[reader](https://github.com/richtamblyn/PGSparkLite/blob/0578a7302ad19fa53665d0fbedfc72c33d4c5c25/lib/external/SparkReaderClass.py),
[device model](https://github.com/richtamblyn/PGSparkLite/blob/0578a7302ad19fa53665d0fbedfc72c33d4c5c25/lib/sparkdevices.py).

- **Proven:** this repository has a Python RFCOMM socket communications path;
  the inspected repository has no ESP32-S3/NimBLE client path. BLE MTU,
  notifications, GATT write type, and BLE connection parameters are not
  applicable to this Python transport.
- **Proven source fact:** its reader has a CMD `04` ACK branch and a CMD `03`
  SUB_CMD `15` effect-state parser. It also handles protocol chunk/sequence
  fields. Its Python communications path echoes a decoded message sequence in
  its conditional protocol ACK. **Speculative:** source parser branches do not
  establish that an amp sent either message in a hardware session.

## Assessment and confidence

- **Proven:** this Spark 2, with Ignitron's current NimBLE/FFC0-FCC2 setup, sent
  one matching generic `04/15` frame for every one of 500 FX commands and no
  `03/15`; a separate live 12-change run reproduced 12/12 `04/15`, 0 `03/15`.
- **Proven:** `spark-go-utils`' ESP32-S3 implementation has a source comment
  reporting the same generic-ACK-only effect behavior on a Spark GO. Its GATT
  UUIDs and write-without-response mode closely match Ignitron; both explicitly
  subscribe with CCCD response.
- **Plausible:** the generic-ACK behavior is Spark firmware/model response
  behavior, not a missed `03/15` caused by Ignitron's current subscription or
  write type. This fits the complete 500-action incoming-frame trace, zero
  rejects, and separate live wire test.
- **Speculative:** different negotiated MTU, connection parameters, OS BLE
  backend behavior, firmware/model revision, or a second central might change
  observed notifications. No A/B capture has tested these, and no MTU/write/
  connection/subscription experiment was performed.

## Desktop Bleak probe prepared

`tools/probe_spark_fx_confirmation.py` is observe-only by default, requires an
explicit BLE address, logs raw notifications to timestamped JSONL, and labels
incoming `03/15` state events and `04/15` generic ACKs. It correlates a response
to the explicit message number and records command-to-response latency when
`--mutate` is enabled with explicit `--effect`, `--state`, and
`--message-number`. The mutation path sends one standard `01/15` state command
and never restores state. Offline tests cover encoding, checksum, split/coalesced
notifications, both response types, and latency correlation.
Observe-only still connects and subscribes to FFC2 (Bleak may write the CCCD),
but sends no Spark protocol/FX command.

Offline validation:

```sh
PYTHONPATH=tools python3 -B -m unittest test_probe_spark_fx_confirmation
python3 -B tools/probe_spark_fx_confirmation.py --help
```

No Bleak scan, connection, or mutation was run: Ignitron was actively connected
to the Spark 2, so a second central could conflict and an appropriate isolated
local adapter was not established. No MTU, write type, interval, or subscription
experiments were performed.

## Most promising later A/B

**Plausible priority:** compare the same Spark 2 from an isolated desktop Bleak
central and an ESP32-S3/NimBLE central while capturing actual raw notifications.
This separates OS/Bleak GATT behavior from ESP32/NimBLE behavior without inferring
wire behavior from different project parsers. The desktop and Ignitron both
request write-without-response; Ignitron and spark-go-utils ESP32 both use the
same FFC0/FFC1/FFC2 service layout and a response-backed CCCD subscription, so
those source settings are not currently a strong explanation for the Spark 2
`04/15`-only trace. The ESP32 clients' connection-parameter settings differ or
are unspecified, but a causal effect is **speculative** until measured on the
same amp. Paulhamsh/PGSparkLite Python RFCOMM paths are not a direct BLE GATT A/B.
