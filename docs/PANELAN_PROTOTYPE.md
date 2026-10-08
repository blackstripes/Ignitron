# PanelLan touchscreen controller prototype

This document records the starting point for the custom Ignitron foot controller.
The normal LVGL target now drives two external minis with separate preset/FX
cards; TFT1 was visually verified before TFT2 was added. TFT2 still needs visual validation;
footswitches and TFT3..6 remain deferred.

## Hardware identified and tested

| Part | Confirmed detail |
| --- | --- |
| Panel/display board | PanelLan ZX2D80CE02S V1.0 / SC05_X |
| MCU module | WT32-S3-WROVER (ESP32-S3) |
| Main display | 2.8-inch, 240x320 ST7789 IPS TFT over 8-bit 8080 |
| Touch | FT5x06 capacitive touch |
| Firmware orientation | Landscape (320x240 logical pixels) |
| Board support | PanelLan Arduino library with LovyanGFX `BOARD_SC05_X` |
| Verified Spark | Spark NEO Core and Spark 2 (tuner path) |

The display is deliberately rotated to landscape in `PanelLanDisplay::begin()`.
LovyanGFX rotates touch coordinates along with the display, so touch targets
are defined in the same 320x240 coordinate system as the UI.

## Build and flash

Use the PanelLan-specific PlatformIO environment:

```sh
pio run -e panelan-lvgl-controller
pio run -e panelan-lvgl-controller -t upload
```

For opt-in preset timing diagnostics on the same SPI mini backend, build
`pio run -e panelan-lvgl-controller-preset-trace` and flash that environment
when ready. Open a 115200-baud serial session and send `preset N` (one command
at a time); no automatic cycling is installed. Filter lines beginning with
`PRESET_TRACE`: `t` is `millis()` (wraps), `id` links accepted requests to
`queued`, `sent`, `phase`, `ack`, `number`, `number_query`, `full_query`,
`full_result`, `number_confirm`, `full_refresh`, `full_data_timeout`,
`full_data_conflict`, `full_query_send_failed` or `fail`. `elapsed` is milliseconds since acceptance;
phase values are Scanning=0, Reconnecting=1, Identifying=2, Syncing=3,
Ready=4. `reject` includes target/confirmed/reason without an id; `late_number`
labels a target observed after failure, **not** a controller confirmation.
Full results carry response `msg` and query `match` (0/1). Only a 03/10 reply
with the message number of the verification query issued after the switch
confirms it; a matching full response refreshes
name/FX separately. A full-data timeout is not a switch failure. Number
observations are snapshot changes, not a complete record of every wire packet.
Startup synchronization emits `startup_full_query` (including send status and
retry), `startup_full_result` (response `msg`, query `match`, and link `ready`),
and `startup_full_timeout` before retrying an unanswered query. These include
the request id for an already-observed accepted target, or id=0 for ordinary
startup sync; `target` is the reported slot at query time. Results are based
on a full-preset observation revision captured before sending. No payload is logged.
After a confirmed switch, the full-preset read and any startup-sync recovery
use a separate **two-second** attempt cadence (also after send failure), not
the five-second switch confirmation timeout. A missing post-switch full reply
revokes both old response expectations before the next startup retry; only a
fresh response correlated to the current query may restore name/FX readiness.
The confirmed selection remains confirmed while full data is stale. A newer
selection or disconnect cancels the schedule. This does not alter number
verification polling, tuner, looper, or cache timing. Host-only cadence
and correlation regression (does not exercise the BLE parser or controller):
`g++ -std=c++17 -Wall -Wextra -Werror -Isrc tools/test_full_preset_retry.cpp -o /tmp/test_full_preset_retry && /tmp/test_full_preset_retry`.
For FX, a matching command ACK is a timing milestone (never confirmation).
The ACK cursor is captured before the FX command is issued, so an
ACK arriving during the send is eligible on the next tick; ACKs already present
before the send are not. NEO Core may omit both ACK and FX_ONOFF. Host-only send-boundary model:
`g++ -std=c++17 -Wall -Wextra -Werror -Isrc tools/test_fx_ack_during_send.cpp -o /tmp/test_fx_ack_during_send && /tmp/test_fx_ack_during_send`.
The first correlated full-preset query is sent after a **one-second grace period
from the FX send**, with or without ACK; failed sends and unanswered queries
are retried no sooner than **five seconds after the previous attempt**, giving
large fragmented replies a full response window before replacement (unlike the
two-second preset refresh schedule). This is an FX-specific response window,
not a parser timeout guarantee. Each replacement revokes the previous response
gate before sending, including on send failure. A fresh direct FX_ONOFF takes
priority; a current correlated full response showing the requested state can
confirm even without ACK. An old-state response to a query issued before the
matching ACK is inconclusive, even if the ACK arrives before its reply; only
an old-state reply to a query issued after that ACK establishes a conflict.
`fx_full_query` trace lines include `ack=0|1` at query send time (also logged
for failed attempts); `fx_full_result` reports the matched query's ACK status.
After an authoritative FX observation, controller state, telemetry, the persistent
`FxConfirmed` event and pending-request cleanup complete before serial output.
With `PANELAN_PRESET_TRACE`, confirmation emits
`event=fx_confirmed slot=<n> msg=<original FX msg> source=FX_ONOFF|full_preset elapsed=<ms>`
after cleanup; `elapsed` is captured at the controller confirmation decision
against the original FX send, before bookkeeping/output. The human-readable
confirmation follows it. Host sequencing and correlation model:
`g++ -std=c++17 -Wall -Wextra -Werror -Isrc tools/test_fx_confirmation.cpp -o /tmp/test_fx_confirmation && /tmp/test_fx_confirmation`.
The FX action remains pending (and preset selection is blocked) until
resolved, with a **15-second** deadline anchored to the original FX command;
ACKs and retries never extend it. A reply at or after the deadline cannot confirm.
FX timeout, conflict or send failure also invalidates the preset snapshot and
revokes both full-query gates; the startup synchronizer, not an unregistered
raw query, retries a correlated full read (including after a failed send).
FX stays unavailable until its matching response restores Ready. Disconnect
still sends no recovery query. Cancellation/disconnect clears the query gate
and schedule. Host-only FX gate
model: `g++ -std=c++17 -Wall -Wextra -Werror -Isrc tools/test_fx_full_preset_retry.cpp -o /tmp/test_fx_full_preset_retry && /tmp/test_fx_full_preset_retry`.
`event=disconnect` is a diagnostic-only, id-free link-loss marker emitted once
when `onAmpDisconnected()` runs after a connected `process()` tick, whether
called directly on a disconnected headless tick or from `process()`, even with
no preset action pending. It is not emitted on every disconnected tick or
before the first observed connection.
In controller mode a 0x38 ACK is only a transport milestone; it never applies
or saves a preset. Verification polls start even if no ACK or number broadcast
arrives. Legacy profiles retain ACK bookkeeping.
Unsolicited 03/38 broadcasts (and older 03/10 replies) still update Spark's
reported slot/snapshot, but cannot themselves confirm a sent command. Final ACK
events are retained through a controller tick, even if unrelated ACKs arrive
afterward. The wire uses a finite message-number space: if an old reply is
delayed across reuse of the same number, the protocol provides no further
identity to distinguish it from the new query's reply. Link/time bounds and
monotonic sequence allocation reduce, but cannot eliminate, that ambiguity.
The normal environment emits no `PRESET_TRACE` lines.

After sending a switch, the controller sends a 02/10 number query on the next
controller tick, even without any ACK/broadcast, and polls at most once per
500 ms until confirmation or the **original five-second switch timeout**.
Failed sends and unanswered replies are retried at that bounded cadence;
there is no duplicate poll while a reply is outstanding within that interval.
Only a fresh 03/10 reply with the current poll's message number and a target
slot consistent with the snapshot confirms. A previous-slot reply releases
the poll for a subsequent attempt, not confirmation. Expired poll replies
cannot confirm a newer poll (subject to finite wire message-number reuse).
The host regression for this poll gate is
`g++ -std=c++17 -Wall -Wextra -Werror -Isrc tools/test_preset_number_verification.cpp -o /tmp/test_preset_number_verification && /tmp/test_preset_number_verification`.
On the connected Spark NEO Core trace run before this polling change, preset 2
was sent at t=8870 with no ACK/number trace until t=13982 (>5 seconds).
The switch timed out; a subsequent reconciliation query (msg12) reported slot
2 in 107 ms and satisfied the deferred latest slot 2. This showed a missing
post-switch verification query, not a failed switch. Later rapid-switch runs
with the polling build confirmed non-noop changes in roughly 0.4–1.3 seconds.

Rapid selections use one latest-wins deferred target. While a command is sent,
new taps replace that target; the next command is dispatched after the current
Spark number confirmation, without waiting for its full-preset refresh. Only
the final confirmed selection triggers a full-preset query. A newer tap revokes
an outstanding refresh; mismatched/late full replies cannot publish active FX
or name data. FX, tuner and looper still require Ready and do not interleave
with preset writes. If the sent command times out without a number, the
deferred selection remains pending. The controller queries the authoritative
hardware number after timeout, retaining the newest accepted target; only a
matching 03/10 reply to that exact post-timeout query (consistent with the
refreshed snapshot) releases the next switch. Late replies to older queries,
broadcasts, and ACKs cannot release it. A silent amp causes bounded query
retries while the intent remains pending; link loss fails it. The timed-out
switch is never confirmed by the reconciliation reply. Trace builds emit
`reconcile_wait`, `reconcile_query`, and `reconciled` milestones.

If the latest deferred target already matches Spark's reported slot, no extra
switch command is sent. Startup sync fetches the full preset; taps during that
Syncing refresh remain eligible as new latest-wins selections. The previous
startup full-query gate is revoked before dispatch, and a selection already at
the reported slot triggers a fresh sync query rather than trusting the old reply.

For an **operator-driven** serial stress run, flash the opt-in trace firmware
first, connect the Spark amp, and provide serial access to the actual USB CDC
port (opening it may reset the board):

```sh
python3 tools/stress_panelan_presets.py --port /dev/your-device --sequence 2,3,4,2,3,2 --interval 0.15
```

For the separate **six-slot FX hardware acceptance run**, with the PanelLan
controller firmware already running and an amp connected, use one exclusive USB
CDC serial session (115200 baud):

```sh
set -o pipefail
python3 tools/stress_panelan_fx.py --port /dev/your-device --probe-preset 2 2>&1 | tee panelan-fx-run.log
```

The script does **not** flash, save, write presets, or detect a port. It polls
`status` until Spark is connected and Amp, Serial, and Preset name are known,
then sends only `fx gate|comp|drive|mod|delay|reverb toggle` (in that order),
twice per slot. It requires a slot/model/direction send line and a subsequent
matching `confirmed by FX_ONOFF` or `confirmed by full preset response` line for
each action **and** a `Message processed:` Spark payload with the exact model's
`Effect`/`IsOn` or full-preset pedal `Name`/`IsOn` matching the sent direction.
A controller confirmation alone is inconclusive. It verifies the second
requested direction is opposite. It waits
for ready status between actions and prints a PASS per slot. Default first-action
busy probes send an immediate duplicate toggle after the first send and require
its explicit rejection **before** confirmation; a too-fast confirmation is
inconclusive, not a pass. `--probe-preset 2` additionally probes preset rejection
while FX is pending (slots 1..8 valid); this requires the opt-in preset-trace
firmware to correlate `PRESET_TRACE event=reject target=2 reason=fx` with the
CLI rejection. Without that option no preset command is sent. Use
`--no-busy-probes` to skip busy probes (and do not supply `--probe-preset`).
For this FX policy, allow more than 15 seconds per action (the harness default
`--timeout` is 18 seconds); `--ready-timeout` defaults to 30.
Capture stdout and stderr for diagnosis; `set -o pipefail` preserves the harness
exit status through `tee`. Any reject,
cancel, missing/crossed confirmation, lost readiness, timeout, or probe race
exits nonzero and **never sends a compensating toggle**. A failed run can leave
an FX changed; inspect the amp before rerunning. Keep touch/other serial clients
idle throughout; unsolicited FX changes can invalidate the result. This is an
operator-run hardware check, not a host simulation or proof of BLE behavior.
An earlier six-slot run with two-second FX full-query retries stopped at
compressor-on after overlapping fragmented replies were rejected as malformed.
After lengthening the FX response window and adding no-ACK query fallback, a
trace run completed all six slots with two confirmed inverse toggles each. The
script verified both the controller confirmation and the returned Spark
`IsOn` value. On delay-on, two earlier replies were absent/malformed; the third
query produced a matching full payload and confirmation at about 11.5 seconds,
inside the 15-second deadline. Gate, compressor, drive, modulation, and reverb
also completed both directions. Busy-duplicate probes had separately passed
on the first four slots. The normal non-trace firmware was then restored and
reported Spark connected, preset 1 CLEAN. The host harness still cannot prove
audio output; it verifies Spark-owned protocol state.

Follow-up after the controller full-preset response-lane deadline was shortened
to 1500 ms: a paced run on normal firmware completed all six FX slots in both
directions. All 12 actions were confirmed by a matching full-preset response
(the returned pedal `Name`/`IsOn` matched the requested direction); the harness's
duplicate-toggle busy probes were rejected. The amp remained connected, and
device diagnostics showed 18 lifetime FX sends / 18 confirmations, zero FX
failures, and zero transport retries/timeouts. This is a focused 12-action
verification of the full-preset fallback path, not a long FX soak or audio-output
test. The normal non-trace firmware remains installed.

The stdlib-only script does not flash or auto-detect hardware. It prints each
trace line and per-accepted-request `number_confirm_ms` (accept to correlated
slot reply) and `full_sync_ms` (accept to matching parsed full response and
renderer-facing Ready); missing milestones print `-`. `slow_full_sync=True`
flags Ready after more than 1000 ms even on successful selections. It also
prints sent and failed status, rejects, and `final_synced_target`; `none` means the final
accepted selection/no-op lacks authoritative number evidence and a matching
full refresh (or ready, matching startup full result), or a later number reports
another slot. A timed-out query does not count, but a subsequent query and
matching response can recover. Any id=0 startup query revokes earlier
readiness, even if it queries another slot; a sent, matching, ready id=0
result restores it only for the latest preceding id=0 query for the final
slot. A later number observation of another slot also revokes readiness and
outstanding refresh correlation: returning to the final slot requires a new
full query and matching full result. A `disconnect` after the final action's
number/readiness marker also revokes full readiness and all outstanding query
correlation, even after `full_refresh` and without a pending action. Only a
subsequent current-link query paired with a matching ready full observation
can restore the stress-run pass result. A `satisfied` deferred selection waits
for its startup full result; an `already_current` rejection can reuse existing
full readiness only when a startup result is paired with an observed query of
the same message number (or a matching full query/refresh). The lower-level
number-only helper is not the stress-run pass result. Start only when the link
is Ready. The
host-only queue test is `g++ -std=c++17 -Isrc tools/test_preset_target_queue.cpp
-o /tmp/test_preset_target_queue && /tmp/test_preset_target_queue`. It covers
the deferred-target-equals-confirmed transition and the startup-query busy
gate. The deterministic multi-request host regression is:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -Isrc tools/test_preset_orchestration.cpp -o /tmp/test_preset_orchestration && /tmp/test_preset_orchestration
```

It is a host protocol *model* that composes the production queue, ACK matcher,
receive history and reconciliation gate, then injects rapid replacements,
stale ACKs and number replies, unsolicited broadcasts, an obsolete full
response and a timed-out switch with a deferred latest intent. Its driver
reimplements those event boundaries; it does not execute or regression-test
`ControllerActions.cpp` or `SparkDataControl`. In particular, the full-response
check only verifies message correlation, not payload decoding/publication; the
model uses an ACK-triggered query and does not simulate the polling cadence.
It cannot prove BLE delivery, scheduling latency or actual amp behavior. Wire
reply ordering and real-world timing still need the hardware run.

On BLE loss, the headless loop clears controller actions before its early return:
sent/queued/deferred presets, timeout reconciliation and query gates are revoked
without sending any Spark commands. An in-flight sent preset retains its failure
event; an unsent selection is discarded without a send failure. FX cancellation
retains its failure event. The host-only reset regression is
`g++ -std=c++17 -Isrc tools/test_preset_link_reset.cpp -o /tmp/test_preset_link_reset && /tmp/test_preset_link_reset`.

Hardware regression gap (the host model does not run the BLE/protocol/controller loop):
with a ready link, inject/tap P3 after another preset. Verify `sent`, Spark
correlated 03/10 reply and `number_confirm` on reported number 3 clear pending
without RETRY (ACK is optional with proactive polling);
withhold the matching msg12 full-preset response for >2s and verify
`full_data_timeout` (not `fail`), Syncing/stale name and unavailable FX, then
a retry query. Deliver a matching full response and verify FX becomes known.
Separately test no number (bounded `number_timeout` and RETRY), wrong number
(`conflict_number` and RETRY), send failure, disconnect before number, and a
late full response after a newer request (must not clear its pending state).
Build checks alone cannot validate these hardware sequences; do not infer that
the missing full response has been fixed at the protocol layer.

It is configured for the ESP32-S3, 8 MB flash, QSPI PSRAM, and USB CDC. The
current development board is exposed on macOS as `/dev/cu.usbmodem1101`; update
the `upload_port` / `monitor_port` in `platformio.ini` if macOS assigns a
different device name.

For an isolated hardware check that excludes the Spark stack:

```sh
pio run -e panelan-display-bringup -t upload
```

For a **separate, single external 0.96-inch ST7735S** test (not the built-in
PanelLan LCD or controller firmware), build `panelan-mini-tft-bringup` and, when
ready to flash, choose the actual USB device explicitly:

```sh
pio run -e panelan-mini-tft-bringup
pio run -e panelan-mini-tft-bringup -t upload --upload-port /dev/your-device
```

Power the module externally from **3.3 V** with common GND; connect IO10 to
SDA/MOSI, IO11 to SCL/SCK and IO14 to DC. IO12 is now I2C SDA and IO13 I2C SCL
to the Waveshare MCP23017 at 0x27 (A0/A1/A2 left open/high): GPB0 drives mini #1 CS and GPB6 drives RES. Provide
3.3 V pull-ups on I2C; this standalone test selects TFT1 only, even if TFT2
is wired to GPB1. GPB2..5 are reserved high for future mini CS lines.
Do not connect RES/CS directly to IO12/13. No MISO is
used. Connect BLK to external 3.3 V (the standalone test displayed colors only
after this connection); firmware never drives the backlight. Serial at
115200 reports the selected settings and completion of the draw commands, but
cannot confirm pixels without readback. Look for six RGB/yellow/magenta/cyan
bars, full borders, and `ST7735S OK` text. The tested 80x160 portrait profile
uses ST7735S RAM offset (26,1) and rotation 0; other module variants may need
different offsets, color inversion/order, or rotation if the border is clipped,
colors are wrong, or the image is oriented differently.

Use a known-good USB data cable. Opening a serial monitor can reset the board
on this USB CDC setup, so the touchscreen is the preferred control surface for
normal Spark testing.

## Current UI and behavior

- LVGL player UI with Home/Preset, FX, gated Looper, Tuner, and Device screens.
- Preset and FX requests travel through `ControllerActions`; the visible value
  stays pending until a fresh Spark-owned observation confirms it.
- Spark 2 tuner supports display entry/exit and external observation, live
  note/cents rendering, native amp mute, and stable bottom navigation. A
  tuner-destination tap exits tuner before opening that destination.
- Device shows Spark model, amp serial, Bluetooth connection state, and tone
  freshness. Multi-device selection is still pending.
- On a Spark reboot, the BLE client refreshes GATT services and subscribes
  again before it considers the connection usable.
- The normal `panelan-lvgl-controller` initializes **TFT1 and TFT2** on shared
  IO10=MOSI, IO11=SCK, IO14=DC, MCP23017 GPB0=TFT1 CS, GPB1=TFT2 CS and
  GPB6=shared RES;
  IO12/13 are I2C SDA/SCL (no MISO). BLK remains
  externally tied to regulated 3.3 V for maximum backlight, with common ground.
  The 2.8-inch panel backlight is set to brightness 255 (maximum); user
  brightness adjustment is deferred. The default renderer
  uses hardware SPI2 (LovyanGFX, mode 0, 10 MHz), not LVGL. The wall-powered
  `panelan-lvgl-controller-spi-mini` build was visually verified with the main
  touchscreen: the mini shows `P1` on Preset and changes to `GATE` on FX.
  That same SPI backend is now the default `panelan-lvgl-controller` build.
- This mini currently labels **touchscreen context**, not an actual footswitch
  or a completed performance mode. The preset card shows the dominant preset
  name from the checksum-validated per-amp hardware-slot cache, even when P2
  is active (for example P1 `CLEAN` remains `CLEAN` after selecting P2
   `CRUNCH`). It shows `UNKNOWN` while slot 1's name is unknown, never the active
   preset's name or a misleading `P1`. TFT2 independently shows slot 2's name
   (or `UNKNOWN`) and selection state. The GPIO fallback follows TFT1's rule.
   The controller
  loop asynchronously fetches missing slots one at a time after identity,
  checksums, current number and full-preset startup sync are ready. A slot gets
   at most three queries per scan (five seconds between attempts); a completed
   scan waits 30 seconds before revisiting missing slots. Every final timeout
   releases the outstanding transport request before moving on. Missing startup
   serial/checksum metadata is retried on the idle path at five-second intervals.
   User actions take priority and pause further cache sends. A
  checksum invalidation restarts the bounded scan. Normal request numbers skip
  reserved 0xEE; only a matching outstanding 0xEE slot read on the same amp
  serial and link can populate the cache. Timeout/retry replaces the outstanding
  slot identity (the protocol cannot distinguish two retries of the same slot).
  Disconnect resets this scan;
  cached files are scoped by amp serial and validated against amp checksums on
  each link. A matching cache response never switches the active preset or
  confirms a user action. Names appear through snapshot revisions as they
   arrive; there may be a delay or `UNKNOWN` if the amp never replies. Last-known
   nonempty names survive cache misses within the same amp identity/link, but
   disconnect or identity changes clear them. Pending,
  failed, stale/unknown, selected and nonselected states use amber dots, red
  exclamation, amber dash, filled gold waveform strip and muted outline/dash,
  respectively. A failure on another preset does not claim P1 was targeted.
  TFT1 FX shows `GATE` and TFT2 shows `COMP` (logical FX slots 0 and 1), each
  with a large green `ON` or blue-grey `OFF` filled strip when
  confirmed; pending/failed/unknown use amber dots, red exclamation or amber
  dash without asserting ON/OFF. Looper is labeled `UNWIRED` (no switch action),
  tuner shows a large note and cents only with a fresh active sample (otherwise
  a muted or amber `TUNER`/dash), and Device uses link colors/short labels or
  dots/dash while unconfirmed. No sync/retry/current text appears on the mini.
  Its 160x80 artwork is based on directly viewing all three local concepts:
  `/home/pzwolinski/Desktop/Ignitron/miniUI.png`,
  `/home/pzwolinski/Desktop/Ignitron/miniUI2.png`, and
   `/home/pzwolinski/Desktop/Ignitron/miniUI3.png`. The first two guide the
   preset/FX name, waveform and status strip; the third illustrates utility
   concepts only, not implemented controls.
  The near-black card has a bright two-pixel colored outline and a wide lower
   strip; state is expressed by fill/color and marks rather than small diagnostic
   copy. Names use measured built-in proportional 18pt/12pt bold fonts with
   ellipsis as needed; unsupported non-ASCII characters become `?`. Other pages
   remain honest context cards, not the utility actions pictured in miniUI3.
   Cards draw directly to `PanelLanMiniSPI` over SPI2 using the validated
   primitives; the 160x80 sprite/`pushSprite` path was removed after it produced
   diagonal artifacts. The direct-drawn artwork has now been visually checked:
   the diagonal streaks and left-edge rainbow column are gone. Cache keys cover
   the preset name, label, status, colors, icon/layout, and touchscreen view
   independently for each mini. No
   new assets or graphics libraries are required.
   No pedal input or looper control is assigned to it. Main-panel touch/page
   switching and mini updates have been verified together. Spark BLE
   responsiveness with the revised mini artwork active has not yet been
   specifically retested.
  Other views retain the same touchscreen-context card on both minis. This is not a
  change to the frozen 2.8-inch LVGL touchscreen UI or an implementation of the
  six-display layout.
  **Keep the 2.8-inch visuals and behavior frozen until all six mini displays
  work.** TFT3..6 and footswitches remain future work; TFT2's output
  requires visual verification on the connected hardware.

The original OLED, LEDs, and footswitch source files are excluded from this
target because their legacy GPIO initialization overlaps the PanelLan LCD bus.

### Mini coexistence and transport profiles

The explicit SPI profile is an alias of the default integrated LVGL/controller
build (state-fed TFT1/TFT2 cards, not the standalone TFT1-only test):

```sh
pio run -e panelan-lvgl-controller-spi-mini
```

Use the IO10/11/14 and MCP23017 wiring above and regulated external 3.3 V for VCC and BLK,
common ground, and the stable 5 V wall supply for the PanelLan board; do not
connect two USB power sources simultaneously. Both SPI controller targets exclude
the GPIO renderer and raw trace commands. They use the standalone LovyanGFX ST7735S
SPI2_HOST mode-0 10 MHz profile (80x160 glass, portrait offset 26,1, rotated
90° CW to 160x80 logical landscape; the integrated mini starts at landscape
X offset 0 to avoid its observed left-edge RAM column). LovyanGFX does not
own CS or reset pins: the MCP driver preloads all GPB0..6 high, pulses shared
GPB6 reset with all CS high, then brackets each controller initialization and
draw by deasserting all CS, selecting GPB0 (TFT1) or GPB1 (TFT2) only, and
deasserting all CS afterwards. The two displays are driven sequentially, never
selected together; transient select/deselect failures retry without permanently
disabling rendering, and each panel caches its card only after its own draw
finishes. GPB2..5
remain high; TFT3..6 and expander switch wiring are not implemented. Each mini
immediately receives six solid
color bars, borders and `MINI` using LGFX primitives, holds that image for at
least five seconds, then draws dark state-driven cards with slot/FX cues and a
  full-width status strip over SPI only when visible content changes (including the
displayed preset name). There is no utility-mode card or bank/TAP/scene/mute
action: those physical functions and performance modes are not implemented.
No pixel readback exists: the wall-powered integrated SPI profile was visually
verified on Preset and FX with the main touchscreen before this artwork update.
A successful build or serial draw
message alone does **not** establish that SPI pixels are visible on other setups.
The standalone `panelan-mini-tft-bringup` and GPIO fallback remain separate;
do not use trace raw GPIO commands with an SPI controller target.

The earlier integrated LovyanGFX SPI2 mini renderer stayed black. The trace-only
GPIO raw full-RAM rainbow then rendered visibly with the wall supply, and the
direct-GPIO state renderer was visually verified on Preset and FX. The newer
integrated SPI profile is now also visually verified under wall power; the exact
reason the earlier SPI2 attempt failed remains unknown. With PC USB
power VCC/BLK measured 1.26 V; with the 5 V 1 A wall supply both measured 3.3 V.
Use the wall supply for hardware testing and do not connect both USB power
sources simultaneously.

Source trace against installed LovyanGFX **1.2.30** and PanelLan:

- `PanelLan/src/board/sc05_x/sc05x{.cpp,_pin.h}` assigns the main screen to
  `Bus_Parallel8` (LCD_CAM), touch to I2C1 on 8/9, and exposes IO10-14 externally.
  There is no main-screen SPI2 owner or pin overlap in that profile.
- `PanelLanLVGLUI::flushDisplay()` ends its transaction before marking LVGL
  ready. Main and mini rendering run sequentially on the Arduino task. The
  default mini path uses hardware SPI2; the GPIO fallback and trace profiles
  bitbang and never configure the SPI mini bus.
- Setup initializes main/LVGL objects, controller/filesystem/BLE, then mini and
  its first frame. BLE scanning already runs asynchronously during mini setup.
  Loop services the event log, checks BLE, refreshes state, updates main then
  mini. Disconnected operation returns after CLI + 10 ms delay; connected
  operation also processes messages/actions and updates both screens again.
  BLE connection establishment can block (30-second connect timeout), delaying
  redraws, but does not itself explain disappearance of an already drawn frame.
- GPIO init asserts RES low 20 ms then high 150 ms, replays the installed
  ST7735S command lists (including their delays), disables inversion, selects
  16-bit pixels and rotation-0 BGR in the fallback profile. BLK is never driven
  by firmware.

For the explicit diagnostic recovery fallback, build:

```sh
pio run -e panelan-lvgl-controller-gpio-mini
```

This profile exclusively compiles the direct GPIO mode-0 bitbang renderer, not
the SPI renderer. It writes the 80x160 window at RAM offset (26,1), four rows
per loop iteration, using a small uppercase 5x7 font and the same
`ControllerSnapshot` as the touchscreen. A full frame is 25,600 pixel bytes
plus 440 window-command bytes over 40 chunks; at >=1 us SCK high per bit,
wire time is >=208 ms (estimated ~0.3–0.6 s total, not hardware measured).
State changes restart after a complete frame; unchanged content refreshes
after two seconds. The finished multi-mini design still calls for shared SPI
with per-panel chip select as described in [PROJECT_PLAN.md](PROJECT_PLAN.md).

For optional coexistence diagnostics, build:

```sh
pio run -e panelan-mini-coexistence-trace
```

This inherits the **GPIO fallback** and boots with live state rendering.
Trace commands can pause service or draw reference bars; the trace profile is
not required for normal operation and must not be mixed with the SPI backend.

At 115200 baud issue one command at a time, then inspect/photograph the mini:

| Command | Persistent effect |
| --- | --- |
| `mini status` | JSON state + forced GPIO snapshot, without changing the display |
| `mini main` | Service only main LVGL/touch; leave the mini untouched |
| `mini pause` | Stop main/application service again; leave mini untouched |
| `mini run` | Normal controller loop while holding the current mini card |
| `mini raw-frame` | GPIO-clock full-RAM bars **without reset/sleep-out/display-on** |
| `mini raw-reset` | GPIO reset + ST7735S initialization, then full-RAM bars |
| `mini dynamic` | Resume state-driven mini rendering and the full controller loop |

Raw commands hold the mini card until `mini dynamic`; use `mini main` to
service only the main UI and `mini pause` to pause it again. Do not use this
trace image for timing-sensitive controller operation.

The raw path drives only IO10-14, MSB first, mode 0, at least 1 us per half
clock (the GPIO fallback uses >=1 us high only). It bypasses SPI peripheral/mutex/DMA, LGFX graphics/text/primitives and
cached windows. It explicitly writes RGB565, rotation 0 and **all 132x162 RAM**
as six bright horizontal bands, avoiding dependence on the glass offset. The
reset variant reuses the installed ST7735S initialization table values/delays,
but transmits them independently; CS stays low throughout commands/pixels and
returns high at completion. BLK is never driven.

Every accepted operation reports JSON mode, selected transport and service
state. Raw writes also report attempted byte count and FNV-1a of the byte stream;
`raw-frame` must report **42783 bytes / 3581832715 hash**. These are software
evidence, not a bus analyzer or pixel readback. The portable generator is tested
by `tools/test_panelan_mini_reference.cpp` for initialization delays/sentinel,
window commands, byte order and every one of the 21384 pixels.

`MINI TRACE` prints IO10-14 mux/routing/configuration, output enables and
RES/CS/DC output latches initially and whenever sampled state changes. Samples
bracket main/mini updates and follow event-log service and BLE checks. These
are register observations, not wire-level measurements; unchanged samples do
not rule out short reset pulses, bad SPI traffic, power or backlight problems.

Still verify Spark BLE responsiveness/reconnection with mini redraws active,
and confirm orientation/readability in the enclosure. The mini is write-only;
serial byte counts cannot confirm visible pixels.

### Preset-change trace

For slow or apparently failed preset switches, build and flash the opt-in
`panelan-lvgl-controller-preset-trace` environment. It leaves the renderer and
2.8-inch UI unchanged, and emits numeric `PRESET_TRACE t=<ms> event=<name>`
milestones for accepted/rejected requests, send, ACK, transient connection
phase, observed hardware-preset number, full-preset verification, and final
confirmation/failure (including elapsed time and request ID). It logs no preset
names, amp serials, or raw BLE payloads.
`full_first_notification` carries `at=<ms>` (callback enqueue timestamp),
`id`, and query `msg`; it is the first notification after dispatch, not
necessarily a reply to that query. `full_result`/`startup_full_result` report
parsed responses; `ready` is emitted once per current query when the snapshot
published by ControllerState is actually Ready for the matching response and
slot. Startup retries following a confirmed selection retain its trace ID;
unrelated startup synchronization uses id=0. Compare `at` against the query
dispatch `t` to avoid controller-loop sampling delay. Trace timestamps wrap at
32 bits. Drive it with the existing serial
`preset <n>` CLI command; this exercises the same `ControllerActions` path as a
touch selection. The serial `touch x y` injection can separately exercise the
touchscreen callback. Keep one serial session open during a cycle because
opening the USB serial monitor can reset this board. The first bench-powered
CLI cycle through presets 2→3→4→1 confirmed each change in under 1.1 seconds. A
subsequent touch-injected cycle through presets 4→1→2→3 produced Spark-number
confirmations within 0.3–0.5 seconds, with no switch-failure event. Preset 4's
full-preset refresh timed out at five seconds; a later payload arrived on the
startup-sync retry. That is a data-refresh delay, not a failed selection; FX
remains stale until the matching full preset is restored. Initial taps during
startup Syncing were correctly rejected before send.

## Deferred multi-device connection design

The desired experience supports both a Spark NEO Core and a Spark 2; the
controller must not be permanently locked to either one.

This remains a follow-up after looper/interoperability work:

1. Persist the last successfully connected Spark BLE address, together with
   its displayed model and serial.
2. At boot or reconnect, prefer that remembered address.
3. If it is unavailable, show an explicit `Scan for devices` action rather
   than silently choosing a different nearby Spark.
4. Show discovered candidates and require confirmation before replacing the
   remembered device.
5. Keep the connected model and serial visible at all times.

The current firmware still auto-selects the first nearby device advertising
the Spark BLE service. The identity card makes that behavior visible, but it
is not yet the final selection flow. Do not rely on it in a space with multiple
Spark devices until the remembered-device and confirmation UI is implemented.

## Deferred pedal hardware

The enclosure direction is an HX Stomp XL-style layout with up to eight
footswitches and six small per-switch displays. Beyond the first directly
wired mini, switch inputs, additional mini CS lines, and performance-mode
mapping remain unimplemented. The proposed expansion approach remains in
[PROJECT_PLAN.md](PROJECT_PLAN.md).


## Behavioral spec references

The current hardware prototype should now be evolved using:

- [STATE_MODEL.md](STATE_MODEL.md)
- [INTERACTION_SPEC.md](INTERACTION_SPEC.md)
- [AMP_BEHAVIOR.md](AMP_BEHAVIOR.md)
- [UI_DESIGN.md](UI_DESIGN.md)

The prototype's current direct preset switching is a proven transport/UI checkpoint, not the final state architecture. New controls should route through the shared controller-state/action model rather than adding more direct callbacks from individual UI widgets into SparkDataControl.

## Next controller checkpoint

LVGL 9.x is now running above the known-good PanelLan/LovyanGFX path. Do not
replace that board support. The next controller checkpoint is verified Spark 2
internal-looper capability/state, followed by tap-tempo and interoperability
testing before external physical controls are added.
