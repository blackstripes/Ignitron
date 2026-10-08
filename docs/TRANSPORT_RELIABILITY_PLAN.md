# Spark Transport Reliability Plan

Status: ACTIVE ENGINEERING PRIORITY
Review date: 2026-10-03
Reviewed baseline: main at 5bafc52b3e68e6965cc39b558369c87074edc05e ("fix: improve preset and FX reliability")

## Purpose

This document is the implementation handoff for hardening Ignitron from a prototype that usually behaves correctly into a controller that can be trusted for live use.

The immediate goal is not to add features. The goal is to make Spark communication deterministic, observable, and resilient under rapid user input, fragmented Spark responses, background synchronization, reconnects, and BLE write/receive failures.

Complete this reliability work before adding more Spark-facing features such as tap tempo or expanding looper behavior.

## Executive conclusion

The current ControllerActions and ControllerState architecture is substantially correct and should be preserved.

The current implementation already does the important high-level things correctly:

- Spark-owned state remains authoritative.
- Pending user intent is separate from confirmed Spark state.
- Preset changes are not confirmed by transport ACK alone.
- Hardware preset verification uses fresh correlated 03/10 replies.
- FX changes are sent as explicit desired ON/OFF state, not blind repeated toggles.
- FX confirmation prefers a fresh FX_ONOFF observation and can fall back to a correlated full-preset read.
- Full-preset refresh is separate from hardware-preset-number confirmation.
- Disconnect and stale-state handling are conservative.
- Existing diagnostics and persistent event logging are already useful.
- Existing host and hardware stress scripts should be retained.

Do not rewrite the state model or replace these confirmation semantics.

The remaining reliability risk is primarily below ControllerActions: outgoing Spark traffic is not yet managed by a true transaction scheduler, while incoming Spark replies can be fragmented and are processed by a single response/parser path.

## Findings

### 1. User actions are serialized better than the underlying Spark transport

ControllerActions prevents many conflicting operations, but SparkDataControl still uses one global currentCommand deque.

SparkDataControl::triggerCommand currently replaces that deque:

    if (msg.size() > 0) {
        currentCommand.assign(msg.begin(), msg.end());
    }
    return sendNextRequest();

Multiple code paths can call triggerCommand, including controller actions, verification/synchronization queries, protocol ACK handling, metadata/cache work, looper work, and other background traffic.

This creates a transport-level risk even when the action layer is behaving correctly:

- a new command can replace the remaining parts of an older multi-part command;
- a background request can begin while another response-bearing transaction is still unresolved;
- protocol ACK traffic shares the same command machinery;
- response timing is managed indirectly by action-specific timers instead of by one transport owner.

This is the main architectural item to fix.

Important: this is a demonstrated structural risk, not a claim that every observed user-facing delay was caused by currentCommand replacement. Preserve evidence-based language in code comments and commit messages.

### 2. The repo already contains evidence that overlapping response traffic is harmful

The existing FX hardware notes document an earlier failure mode where two-second full-preset retry cadence caused overlapping fragmented replies to be rejected as malformed. Increasing the FX response window to five seconds allowed a later six-slot hardware run to complete.

That is strong evidence that aggressively sending another response-bearing query while a previous fragmented response may still be arriving can reduce reliability.

The desired fix is not "keep adding arbitrary delay." The desired fix is transport ownership and backpressure so only one controller-originated response-bearing transaction is active at a time unless hardware evidence proves concurrency is safe.

### 3. SparkBTControl::writeBLE can report success after an earlier chunk failed

writeBLE splits a command into BLE-sized chunks and stores each write result into the same return_value variable.

Conceptually:

    chunk 1 -> success
    chunk 2 -> failure
    chunk 3 -> success
    function -> success

Because a later successful chunk overwrites the earlier failure, the caller can receive true even though the full message was not transmitted successfully.

This is a concrete correctness bug.

Required fix:

- fail the whole write immediately on the first failed chunk;
- do not continue transmitting later chunks after a failed chunk;
- record a transport failure with chunk index/count;
- do not report success unless every chunk succeeded.

### 4. writeBLE mutates the caller's ByteVector while transmitting

writeBLE repeatedly erases/slices the input command until it is empty.

This makes retry/debug behavior harder to reason about and prevents the transport from retaining an immutable copy of what it intended to send.

Required direction:

- accept the outgoing bytes as immutable input, or copy once inside the transport;
- iterate by offset rather than destructively consuming the caller's vector;
- retain transaction metadata independently from the mutable BLE library call.

### 5. Incoming notification loss is intentionally fail-closed, but can look like random lag

The NimBLE callback places notifications in a 32-entry queue.

If the queue mutex cannot be obtained within 5 ms:

- ingress busy counter increments;
- the notification is dropped;
- ingress is invalidated.

If the queue reaches its limit:

- ingress full counter increments;
- the entire queued receive stream is cleared;
- ingress is invalidated.

On the controller task, invalidation clears queued messages and resets SparkStreamReader.

That is safer than parsing a corrupted partial response, but one dropped fragment can cause:

    lost fragment
      -> discard/reset partial Spark response
      -> confirmation never completes
      -> wait for retry window
      -> send another query
      -> action appears delayed or failed

Do not simply increase the queue size first. Instrument queue high-water mark, callback contention, and parser invalidation, then determine whether sizing or scheduling is the real problem.

### 6. Actual amp-action latency and controller-confirmation latency are different metrics

An FX command may be applied by the Spark quickly while Ignitron spends seconds proving the new state.

The existing hardware notes include a Delay-ON case that confirmed only after the third full-preset query, around 11.5 seconds after the command. Two earlier replies were absent or malformed.

That does not prove the audible Delay effect took 11.5 seconds to turn on. It proves confirmation took 11.5 seconds.

Measure these separately:

- user action accepted -> BLE write begins
- BLE write complete -> first Spark response
- BLE write complete -> relevant state observation
- BLE write complete -> controller confirmation
- total retries / parser resets / ingress drops

The UI must remain truthful: pending desired state can be shown immediately, but confirmed state must still come from Spark.

### 7. ACK handling should be reviewed as part of the scheduler migration

Incoming Spark messages that require an ACK call handleSendingAck, which currently builds an ACK and sends it through triggerCommand.

Because triggerCommand owns the shared currentCommand deque and also advances the normal nextMessageNum cursor, ACK traffic is coupled to ordinary controller requests.

The scheduler work should explicitly define ACK behavior:

- protocol-required ACKs get highest priority and must be sent promptly;
- an ACK must not destroy or replace an active multi-part transaction;
- ACK generation should not consume ordinary request sequence numbers unless the Spark protocol actually requires that behavior;
- ACK traffic should not be treated as a new response-bearing controller transaction.

Do not change wire numbering blindly. Add a regression test around existing observed protocol behavior before altering sequence allocation.

## Required architecture

Introduce a single Spark transport scheduler/transaction owner below ControllerActions and above SparkBTControl.

The goal is one place that owns all controller-originated writes and their transport lifecycle.

Conceptual flow:

    touch / footswitch / serial / background sync
                       |
                       v
                ControllerActions
                       |
                       v
             Spark transport scheduler
                       |
                 one active transaction
                       |
                       v
                   BLE write
                       |
                       v
               Spark notifications
                       |
                       v
               parser / observations
                       |
                       v
             transaction completion
                       |
                       v
                 next queued work

### Scheduler responsibilities

At minimum, represent each outgoing operation with:

- transaction id
- operation kind
- priority
- outgoing command/message parts
- assigned Spark message number(s)
- whether a response is expected
- expected response identity/type when known
- send start/end timestamp
- response deadline
- retry policy
- completion/failure reason
- link generation so reconnect invalidates old work

Recommended priorities:

1. protocol-required ACKs
2. user performance actions
3. verification queries needed to resolve a user action
4. startup/reconnect synchronization
5. metadata/cache/background reads

Background cache/name reads must yield to user actions.

### Serialization rule

Only one controller-originated response-bearing transaction should own the Spark request/response lane at a time.

Exceptions must be explicit and backed by hardware evidence.

Protocol-required ACKs may use a priority fast path, but they must not overwrite active transaction state.

### Multi-part outbound commands

The scheduler must own the entire multi-part command until it is complete.

Intermediate ACK handling may release the next part, but no unrelated triggerCommand call may replace the remaining parts.

### Completion

Transport completion and controller semantic confirmation are separate concepts.

Examples:

- BLE write success means bytes were accepted by the BLE stack, not that Spark applied the action.
- final Spark ACK may complete a transport exchange but still not confirm a preset or FX state.
- ControllerActions continues to decide semantic success from authoritative Spark observations.

Preserve that distinction.

## Concrete implementation plan

### Phase 0 - preserve and baseline current behavior

Before changing transport semantics:

- build the normal panelan-lvgl-controller target;
- build the preset-trace target;
- run existing host tests;
- record current diagnostics counters;
- retain the current preset and FX hardware stress scripts unchanged as baseline acceptance tools.

Do not begin with timing tweaks.

### Phase 1 - fix BLE chunk transmission correctness

Update SparkBTControl::writeBLE first.

Requirements:

- do not mutate the caller's outgoing message;
- iterate over the message by offset;
- stop immediately if any chunk write fails;
- return false for any partial write;
- retain existing disconnect-on-write-failure behavior;
- add diagnostics for total write failures and failed chunk position;
- keep existing Spark-2 pacing behavior initially so only one variable changes.

Add a host-testable helper if necessary so the middle-chunk-failure case can be verified without hardware.

### Phase 2 - introduce a scheduler without changing ControllerActions semantics

Create a transport/scheduler abstraction and route triggerCommand through it.

Initial migration should be behavior-preserving:

- ControllerActions APIs remain unchanged.
- Existing message builders remain unchanged.
- Existing protocol parsers remain unchanged.
- Existing confirmation/retry logic remains unchanged initially.
- Scheduler owns currentCommand instead of each caller replacing it.

At this phase, prove that unrelated calls cannot overwrite a multi-part command.

Phase 2 implementation contract: the outbound owner retains unsent parts until
their BLE writes succeed (except after a BLE write failure, when the disconnected
transport discards unsent parts rather than replaying a partial command); a
competing ordinary command returns false without
consuming a sequence number. A matching 05/01 intermediate ACK releases one
part. Protocol ACKs write immediately outside this owner and do not create
pending looper requests, but still advance the ordinary sequence cursor once
per ACK as the previous `triggerCommand` path did. The ACK wire number remains
the incoming sequence. After the final part is written the lane is free;
final-ACK/response ownership and backpressure remain Phase 4 work. This
incremental owner does not distinguish duplicate intermediate ACKs (the
protocol observation has no part identity), and a missing intermediate ACK
keeps the unsent parts owned until link reset. Busy ordinary submissions are
rejected synchronously rather than queued, preserving the existing meaning of
the bool send result; callers remain responsible for retaining/retrying intent.

### Phase 3 - migrate every outgoing Spark path

Audit every path that writes to Spark and ensure it enters the scheduler or an explicitly defined ACK fast path.

Include at least:

- hardware preset changes
- FX ON/OFF
- current preset number queries
- full current preset queries
- startup identity/status reads
- cache/checksum reads
- tuner commands
- looper commands/status/config
- protocol ACKs
- any periodic/background reads
- future tap-tempo requests

There should be no hidden direct BLE writer that can bypass transaction ownership.

### Phase 4 - add response-lane ownership and backpressure

Prevent a new controller-originated response-bearing query from starting while the active transaction is still waiting for a fragmented response, unless the active transaction has completed, failed, or expired.

Key rules:

- retries replace an expired transaction, not overlap it;
- late replies to revoked transaction identities remain non-authoritative;
- reconnect clears queued and active link-scoped transactions;
- user actions can cancel/supersede lower-priority background work safely;
- full-preset reads get enough uninterrupted receive time to complete.

Do not remove the existing message-number/revision correlation gates. The scheduler complements them.

Phase 3/4 implementation audit (main e2d56c5 baseline): the only active
controller-to-Spark GATT route is `triggerCommand -> SparkOutbound ->
writeRequest -> sendMessageToBT -> SparkBTControl::writeBLE`. All ordinary
mutations (preset/FX/tuner/looper/settings), full/current-number/identity/
firmware/checksum/amp-battery/looper queries, cache reads and CLI calls use it.
`writeSparkProtocolAck -> sendMessageToBT` is the explicit fast path: ACKs
neither wait for nor release the ordinary owner. AMP-mode `notifyClients` sends
in the opposite direction; there is no other active direct Spark BLE writer.

The owner rejects busy submissions synchronously without advancing the request
cursor. One successfully written 02 query owns the receive lane until its
wrap-safe deadline (originally 5000 ms for all queries; see Phase 6 below),
or until a **complete parsed** 03 response has the same message
number and subcommand, or until revoke, ingress invalidation, or link reset.
Expiry/invalidation/link reset also clear the matching PanelLan full-preset
publication gate before any orphaned late reply can publish active state.
Supported 02 subcommands are 01, 10, 11, 23, 2a, 2b, 2f, 71, 75, 76, 78;
unknown queries fail closed. 03/38 broadcasts and transport ACKs do not
retire 02/10. A 01/38 mutation does not own the response lane: NEO can omit
its final ACK while the controller needs to send 02/10 verification. A 01
mutation may write during a pending 02 response if no multipart parts remain;
a second 02 is rejected without consuming a sequence. Semantic
confirmation remains the controller's own revision/message correlation, not
transport completion. A revoked/expired response with an older normal sequence
cannot release its successor; the reserved EE cache sequence is not reused by
normal requests. Sequence identity alone cannot disambiguate replies if a
number is eventually reused after a full wrap or a repeated reserved EE read;
hardware validation/correlation remains necessary.

The controller's two-second preset retry check does **not** revoke a still-
owned fragmented response: the transport deadline wins. A busy
replacement (including unrelated response ownership) retains the old semantic
and publication gates and does not count as a failed replacement attempt.
Only after that owner expires/completes, or on a successfully dispatched
replacement, may the controller change its expectation. A first or intermediate
multipart BLE write failure clears unsent outbound parts (the BLE transport
disconnects); replay of a partially written command is not automatic.

Busy-intent audit: the connection amp-name read, legacy startup serial read,
post-serial amp-name read, post-name checksums, legacy post-checksum and cache-
miss full-preset follow-ups, looper-entry/controller config+status pairs, and
legacy button looper status/record-status reads retain **named intent** (not built
messages or sequence reservations) and retry from the controller loop. Follow-
ups requested inside response handlers dispatch only after lane release. CLI
`amp` queues serial after a successfully sent name query; `refresh` reports
rejected sends. Queued controller preset/FX/looper/tuner actions keep their
pending intent on multipart-owner Busy; attempted BLE write failures still
fail. A refused tuner write cannot advance the local mode. The
record-start command, looper settings `changePending`, and legacy custom
preset-number follow-up already retain state until a successful send; the amp
battery poll now leaves its due time unchanged on busy. Controller preset/FX
queries and cache slot reads have existing bounded periodic retry/state gates;
controller serial/checksum cache metadata also retries periodically. Diagnostic
and CLI one-shot calls may report busy and require explicit retry; other
multi-step looper commands do not gain atomicity here. No priority queue,
preemption, automatic retry of failed BLE writes, Phase 5 telemetry, or timing
tuning is included in this phase.
Legacy Spark-2 looper *button mutations* remain one-shot: if their first send
is Busy or fails, serial output explicitly says the action was not sent and
requires another press. They are not silently queued because a composite
looper operation with an already-written first subcommand cannot safely be
replayed. A Busy first STOP_REC/STOP_DUB does not schedule a competing status
query; a later failed subcommand after a sent stop may schedule a refresh.

### Phase 5 - instrumentation and tuning

Add compact diagnostics without logging Spark payloads or sensitive device identity.

Per transaction, capture:

- transaction id and kind
- queue time
- BLE send start
- BLE send complete
- chunk count
- first incoming notification time
- complete parsed response time
- controller semantic confirmation time when applicable
- timeout/retry count
- parser reset/invalidation during transaction
- ingress queue depth/high-water mark
- completion/failure reason

Expose aggregate counters through the existing diagnostics command.

Useful latency buckets:

- accepted -> first BLE write
- BLE write -> first response
- BLE write -> complete response
- BLE write -> semantic confirmation

Do not print every raw BLE packet in normal builds.

Phase 5 implementation notes: `diagnostics` now prints lifetime transport
counts, five latency buckets in milliseconds (<100, <500, <2000, <5000,
>=5000), and one last outbound transaction summary. The bounded in-memory
ring retains 16 records but is not dumped on every invocation. Records contain only an internal
monotonic id, query/mutation kind, reason code (Pending=0, Sent=1,
Response=2, Timeout=3, Invalidated=4, Revoked=5, LinkReset=6,
WriteFailed=7, Confirmed=8, Failed=9), `millis()` timestamps, counts and
presence flags; no wire numbers, payloads, model names or device identity.
Queue delay begins at the first Busy rejection of a kind (or at dispatch if
never Busy), not at UI tap time; synchronous Busy submissions are not queued,
so this is a kind-level retry-wait estimate, not a per-action queue timestamp.
BLE completion includes existing pacing and ends with the final successful
part of a multipart command; earlier parts accumulate attempted BLE chunks
but do not increment `sent` or mark the
transaction written. A failed later part increments `write_fail` only.
The active response query retains bounded lifecycle state if its recent ring
record is overwritten, so retries, timeout/termination and response totals
remain accurate; the ring still contains only the latest 16 records. First notification measures the
first newly queued BLE notification after query dispatch (including one
received during the write), not necessarily a correlated
reply; parsed completion requires the matching complete 03 response. If a
notification arrives synchronously before BLE completion its timestamp is
retained but excluded from the post-write latency histogram. Semantic latency
is recorded only for controller preset/FX commands after their existing
confirmation gates; full-preset refresh is not preset action confirmation.
Retries count successfully dispatched replacement verification queries in the
instrumented controller branches, on the replacement's record even if the
prior query has expired; rejected Busy/failed first submissions do not count
as a query to replace. Failed BLE writes report attempted chunks (including
the rejected chunk), while the failure event retains the planned total and
failed position. Semantic confirmation aggregates retain the action's BLE
completion time independently of the 16-record ring until confirmation or
failure; an overwritten record cannot lose its latency/counter observation.
Ingress parser resets are counted once per consumed ingress invalidation even without an
active query; an active owner's record also notes that invalidation. The high-water
mark is the maximum callback queue depth since boot. Counters and records are
in-memory and reset on reboot; link reset terminates an active query without
erasing prior observations. Use hardware traces to interpret distributions;
these buckets are not p50/p95/p99 estimates.

## Current unresolved issue: intermittent user-visible preset synchronization latency

Hardware testing on 2026-10-03 still shows a recurring pattern where most preset
changes feel immediate, but some transitions take roughly 2-3 seconds before all
renderer-visible preset data is synchronized. Treat this as an active reliability
issue even when the preset eventually succeeds.

Important distinction: a hardware preset change and a fully synchronized controller
snapshot are separate milestones. The Spark can report the new hardware-preset
number quickly while Ignitron is still waiting for the matching full-preset response
that supplies the preset name, effect chain, FX state, and the evidence required for
`ControllerState` to return to `Ready`.

Earlier trace evidence demonstrated one concrete failure mode:

- command dispatch occurred about 160 ms after acceptance;
- correlated hardware-preset-number confirmation occurred about 489 ms after
  acceptance;
- the full-preset refresh later timed out after roughly 5.5 seconds;
- stale fragments from one full-preset response were then combined with a retry
  carrying a different message number, producing an invalid 822-byte assembled
  payload;
- receive framing was subsequently hardened so multipart assembly rejects mismatched
  message identities/chunk ordering instead of combining them.

That framing bug is considered fixed, but intermittent multi-second synchronization
delay is still observed in hardware use. Do not assume the remaining delay has the
same root cause.

### Known trace blind spot

The current preset stress summary can under-report what the user actually sees.
Its per-request latency is primarily based on:

    accept -> number_confirm

That measures authoritative hardware-slot confirmation, but not completion of the
subsequent full-preset synchronization. A transition can therefore be reported as
fast even while the UI remains stale/Syncing for another few seconds. Before the
Phase 6 tuning below, a slow full-preset response that completed before the
five-second deadline could produce no timeout or parser-reject event, despite being
plainly visible to the user. Normal-sequence controller 02/01 and hardware-number
02/10 reads now use a 1500 ms response-lane deadline; other queries retain 5000 ms.

For reliability work, track both of these separately:

- **number confirmation latency**: request accepted -> correlated hardware-preset
  number confirmed;
- **full synchronization latency**: request accepted -> matching full-preset response
  parsed and renderer-facing `ControllerState` returned to `Ready`.

A successful action with low number-confirm latency but high full-sync latency is
still a performance problem.

### Required next investigation

Before changing timeout values, retry cadence, outbound scheduling, or controller
confirmation semantics, extend the preset trace so each accepted preset request can
be correlated through the complete user-visible lifecycle.

Capture at minimum:

- accept -> preset command sent;
- command sent -> correlated hardware-number confirmation;
- number confirmation -> full-preset query dispatched;
- full-preset query dispatch -> first BLE notification observed for that query;
- full-preset query dispatch -> complete matching parsed full-preset response;
- complete parsed response -> renderer-facing `ControllerState` becomes `Ready`;
- total accept -> fully synchronized/Ready.

Also record, correlated by preset trace id and full-preset message number:

- whether a full-preset retry was required;
- whether the response lane was busy/backpressured;
- expected and received multipart chunk counts where available;
- framing reset/restart/revoke events;
- ingress invalidations/drops;
- whether a late response arrived after the active query was replaced or revoked.

Update `tools/stress_panelan_presets.py` so its per-request summary reports at least:

    number_confirm_ms=<...>
    full_sync_ms=<...>

and explicitly flags any action whose full synchronization exceeds 1000 ms even if
it succeeds before timeout.

If practical, emit a trace event when the renderer-facing snapshot first becomes
`Ready` for the request so instrumentation measures the same transition visible on
the main TFT and mini displays.

### Diagnostic interpretation

The next captured slow transition should distinguish these cases:

1. **accept -> sent is slow**: scheduler/backpressure or dispatch delay.
2. **sent -> number_confirm is slow**: preset command or number-verification path.
3. **full query -> first notification is slow**: Spark/transport is slow to begin the
   full-preset response.
4. **first notification -> complete parse is slow**: fragmented response delivery,
   callback/queue service, or receive assembly is the likely bottleneck.
5. **complete parse -> Ready is slow**: controller publication/state-transition
   logic is the likely bottleneck.

Do not tune around the symptom until one or more visibly slow transitions have been
captured with these phase timings. The five-second response-lane deadline was
preserved while collecting this evidence; see the Phase 6 result below.

### Phase 6 - tune only after data exists

After the scheduler is stable, use hardware traces to revisit:

- 500 ms preset-number poll cadence
- two-second preset full-refresh retry cadence
- one-second FX query grace period
- five-second FX response window
- 15-second FX overall deadline
- Spark-2 80 ms BLE pacing

Do not shorten these merely to make the UI look faster. Tune from measured completion distributions.

Phase 6 response-lane tuning (hardware trace with `PANELAN_PRESET_TRACE`):
several controller full-preset replies delivered multipart chunks 1..16 of
expected 17 and then stalled. At the five-second lane expiry/retry, a stale
invalid frame was discarded, the old 16/17 assembly was discarded on the new
start, and the retry completed all 17 chunks in about 0.4 seconds. Other full
responses completed in about 0.5 seconds; preset-number confirmation remained
about 0.4 seconds. The observed roughly six-second UI delay was the five-second
response wait plus retry, not slow preset-number confirmation. No queue drops
or ingress invalidation were observed in this trace.

The initial Phase 6 tuning set only the normal-sequence controller full-preset
02/01 response lane to 2500 ms (wrap-safe). Other 02 queries, including the
reserved EE background slot read, retain 5000 ms. The existing two-second
retry cadence still waits for the lane to release before sending another query;
expiry clears the owner
and matching publication gate, and complete parsed replies still require
message-number/subcommand correlation. ControllerActions' semantic confirmation
gates are unchanged. This narrows the wait on the observed stalled 16/17 case
by about 2.5 seconds without intentionally overlapping responses. It is a
targeted recovery adjustment, not hardware soak acceptance or evidence that
all future full responses complete within 2500 ms.

Follow-up hardware test at 2500 ms: of seven requested selections, five reached
Ready in 0.89–0.98 seconds. Two stalled at 16/17 response chunks and recovered
at 3.43–3.47 seconds: the existing two-second FullPresetRetry cadence could not
send its retry until after the 2500 ms lane expired. Normal non-EE 02/01 lane
expiry is therefore tightened to 1500 ms, before the existing retry cadence;
ordinary full replies measured about 0.5–1.0 seconds end-to-end through parse,
leaving headroom. Other query deadlines remain 5000 ms. The lane still prevents
overlapping queries, expiry clears the matching publication gate, and correlated
complete replies and controller semantic gates remain required. This is targeted
recovery tuning, not reliability acceptance; continue hardware validation.

Subsequent preset soak stopped on failure at action id78 (`number_timeout` at
5.16 s); this is not an acceptance run. The preset mutation was sent and ACKed,
and a correlated 02/10 verification query was sent 244 ms later. The frame parser
saw a three-byte invalid-wire fragment, but no correlated 03/10 reply arrived.
Every 500 ms number poll attempt was rejected Busy while that 02/10 owned the
response lane for 5000 ms. A later 03/10 observation during reconciliation showed
slot 6, suggesting the amp applied the target, but it did not semantically confirm
the timed-out action. Measured typical number responses were under 500 ms, so
normal (non-EE) 02/10 queries now release the lane after 1500 ms, allowing the
existing poll cadence to retry before the 5000 ms action deadline. Normal 02/01
remains at 1500 ms; all other queries, including reserved EE, remain at 5000 ms.
Serialization, correlated complete replies, and semantic confirmation are unchanged.
This is evidence-based recovery tuning, not hardware soak acceptance; repeat the
soak to validate it.

Follow-up soak on the connected Spark NEO Core, using the 1500 ms 02/01 and
02/10 deadlines and a 3.5 s command interval, ended when the BLE link dropped.
The trace identified this amp as a four-slot device: requested slots 5–8 were
rejected as out of range and were not sent to Spark. Before link loss, 78
accepted requests reached renderer-facing `Ready`; their full-sync latency
distribution was mean 1334 ms, p50 962 ms, p95 2974 ms, p99 4165 ms, max 5081 ms.
There were 14 full-preset timeouts followed by 16 dispatched retries, no
hardware-number timeout, and no controller action failure before the disconnect.
The trace repeatedly showed initial multipart responses stopping at 13/16 or
16/17 chunks; retries recovered. The operator reports finding the amp powered
down after these long runs and restoring it by powering it back on; the serial
trace itself establishes only the BLE disconnect, not the amp's internal cause.
This was not a 500-selection acceptance run. After the test, normal firmware
was restored and the controller reconnected when the amp was powered back on.

FX follow-up on the same normal firmware with the 1500 ms non-EE 02/01 lane:
the six-slot hardware harness completed both directions on all slots (12 actions).
Every FX action was confirmed by a matching full-preset response, including the
Spark pedal `Name`/`IsOn` observation; busy duplicate toggles were rejected. No
transport retry/timeout or FX failure was recorded during this run. This validates
the short full-preset fallback path used by FX, but is not a 500-action FX soak or
audio verification; keep the longer FX soak as open reliability acceptance.

## UI behavior during this work

Do not redesign the 2.8-inch UI.

Preserve the existing truth model:

- confirmed state stays Spark-owned;
- requested state can be rendered optimistically as pending;
- failure reverts to the last confirmed state and shows failure;
- stale/unknown must never be rendered as OFF.

For FX specifically, the mini/main UI may display the requested ON/OFF value immediately with an obvious pending marker while confirmation is outstanding. This is a presentation improvement, not a replacement for Spark confirmation, and is secondary to transport hardening.

## Required regression tests

Add host-level tests for at least the following cases.

### Outbound transport

- multi-part command cannot be overwritten by a second ordinary request;
- protocol ACK can be sent without destroying the active transaction;
- middle BLE chunk failure fails the entire write;
- no later chunks are sent after a chunk failure;
- caller's original ByteVector remains unchanged after write;
- high-priority user work runs before queued background/cache work;
- reconnect invalidates active and queued link-scoped transactions;
- timeout releases the response lane exactly once;
- retry does not overlap the previous transaction;
- message-number correlation survives queueing and retries.

### Incoming/response interaction

- a fragmented full-preset response can finish without a replacement query starting;
- stale late replies cannot confirm a newer transaction;
- ingress invalidation fails/retries safely;
- parser reset cannot publish a partial preset as authoritative;
- unsolicited Spark state updates still reach ControllerState while no controller transaction owns them.

### Existing controller semantics

All current preset/FX state-model tests must still pass.

Do not weaken confirmation rules simply to make transport tests green.

## Hardware acceptance

Run with the real UI and mini-display renderer enabled, not a stripped-down transport-only firmware.

### Preset soak

Run at least 500 hardware-preset selections, including:

- normal spaced presses;
- rapid latest-wins sequences;
- repeated selections;
- transitions across all available hardware slots.

Acceptance:

- zero incorrect final preset states;
- zero action reported confirmed without authoritative correlated evidence;
- zero controller-generated overlapping full-preset queries;
- zero unexplained parser invalidations;
- no ingress busy/full events during the clean soak target;
- failures, if intentionally induced, recover to truthful state without reboot.

### FX soak

Exercise all six FX slots repeatedly in both directions. Target at least 500 confirmed FX actions total.

Acceptance:

- zero incorrect final FX states;
- no blind compensating toggle on timeout;
- no controller confirmation without matching Spark-owned observation;
- retries remain serialized;
- no self-induced fragmented-response overlap;
- timeout/failure always triggers truthful resynchronization.

### Reconnect soak

Perform repeated amp/controller link-loss and restore cycles.

Acceptance:

- no pre-disconnect transaction can confirm after reconnect;
- fresh identity/current preset/full preset are required before Ready;
- pending operations fail/clear deterministically;
- background queue resumes only after the new link is synchronized.

### Latency reporting

For the hardware soak report p50/p95/p99 for:

- accepted action -> first BLE write;
- BLE write -> authoritative observation;
- BLE write -> semantic confirmation.

Initial target for interaction responsiveness: accepted action -> first BLE write should normally remain below 100 ms at p95 with the full UI active. Do not set hard confirmation-latency thresholds until measurement shows the Spark's real distribution.

## Diagnostics to watch immediately

Before and during implementation, capture the existing diagnostics line after a good session and after any bad session.

Pay special attention to:

- ingress busy
- ingress full
- BLE disconnect/reconnect
- controller preset send / confirm / fail
- controller FX send / confirm / fail
- completed full preset count

If ingress busy or ingress full increments during a delayed/failed action, preserve the event log and trace around that occurrence.

## Non-goals / guardrails

- Do not rewrite ControllerState.
- Do not replace ControllerActions confirmation semantics with transport ACK success.
- Do not turn FX commands back into blind toggles.
- Do not add arbitrary sleeps throughout the code as the primary fix.
- Do not simply increase the incoming queue size without measuring queue pressure.
- Do not redesign the frozen main touchscreen UI during transport work.
- Do not bundle looper/tap-tempo feature work into the reliability commits.
- Do not remove the serial CLI or persistent diagnostics.
- Keep classic/upstream builds compiling where practical; isolate PanelLan/S3-specific behavior.

## Files that deserve first review

Start with:

- src/SparkDataControl.cpp
- src/SparkDataControl.h
- src/SparkBTControl.cpp
- src/SparkBTControl.h
- src/SparkStreamReader.cpp
- src/controller/ControllerActions.cpp
- src/controller/ControllerActions.h
- src/controller/PresetNumberVerification.h
- src/controller/FullPresetRetry.h
- src/controller/FxFullPresetRetry.h
- docs/STATE_MODEL.md
- docs/PANELAN_PROTOTYPE.md
- tools/stress_panelan_presets.py
- tools/stress_panelan_fx.py
- existing host tests under tools/test_*

## OpenCode start instruction

When starting this work, read this document in full, then read docs/STATE_MODEL.md and the reliability sections of docs/PANELAN_PROTOTYPE.md. Inspect the current main implementation before editing.

Work in small, reviewable phases. Prefer one concern per commit.

Recommended first implementation commit:

1. add/extend host tests for BLE chunk failure and command replacement;
2. fix SparkBTControl::writeBLE so partial writes cannot return success and the input is not consumed;
3. add any required transport diagnostics;
4. run existing host/build checks;
5. stop and summarize evidence before starting the scheduler migration.

After that, implement the scheduler incrementally. Preserve ControllerActions and ControllerState behavior unless a failing regression proves a change is required.

The success criterion is not "fewer visible errors." It is deterministic command ownership, truthful state, measurable latency, and zero incorrect final state across long hardware soak runs.

## Spark 2 preset soak attempt (2026-10-06; stopped early)

Run conditions: `main` at `f9c748c`, canonical PlatformIO 6.2.0, connected
Spark 2, `panelan-lvgl-controller-preset-trace`, existing
`tools/stress_panelan_presets.py`, 1–8 cycling at a two-second interval. The
sequence included repeated-current probes. The controller initially reported
slot 8 after the pre-run status had reported slot 4; the first scripted slot-4
selection was therefore a changing action, not a no-op.

The run was stopped for review before completing 500 actions. The full serial
trace is retained in the session capture at
`/tmp/opencode/spark2_soak500.log` (50,645 lines at capture). The client had
issued through `command step=212`; trace review found earlier full-preset
readiness failures, so the soak was not continued and no firmware behavior was
changed. The rapid/latest-wins phase was not run.

Partial-run counts and observations:

- **211 accepted changing actions were dispatched** (`sent` events, action IDs
  49–259); one additional repeated-current request was rejected as
  `already_current`; no synchronization-busy CLI rejections were recorded.
  The stress tool uses `queued` as a fallback when an `accept` trace line is
  missing, so the lower raw `accept` line count is not the action denominator.
- 210 actions had `number_confirm`; 206 reached `ready`. Four number-confirmed
  actions had no corresponding `ready` in the captured run: IDs 95, 131, 179,
  and 239. ID 131's full query reported `sent=0`; IDs 95, 179, and 239 had
  `sent=1` full queries. ID 239's 17/17 multipart assembly completed, but no
  matching controller `full_result`/`ready` followed before a later action.
  No explicit controller `fail`, `full_data_timeout`, or
  `startup_full_timeout` event was logged for these cases. No successful
  same-action recovery to `Ready` was observed for those four IDs.
- Among observed confirmations, `number_confirm_ms` (median p50, nearest-rank
  p95/p99) was p50 **329 ms**, p95 **424 ms**, p99 **508 ms**, max **546 ms**.
  Among the 206 `ready` results, `full_sync_ms` was p50 **1059 ms**, p95
  **1169 ms**, p99 **1255 ms**, max **1269 ms**; **206/206** exceeded 1000 ms.
- Explicit full-preset timeout events: **0**. Explicit dispatched full-query
  retry events: **0**. The since-boot transport diagnostic later reported
  retry=1 and timeout=3, but these counters span earlier activity and cannot
  be attributed to this measurement window.
- `frame_discard`: **34**, all `invalid_wire`, three buffered bytes, no safe
  header identity. They occurred in two bursts (17 each) after full queries
  for IDs 95 and 179. For ID 95, query message 247 had a first-notification
  trace, followed by ingress IDs 3677–3710 being seen/enqueued/dequeued; the
  17 three-byte discards used odd IDs 3677–3709. No valid frame with message
  247, no multipart start, and no `ready` for action 95 were logged. This is
  not the NEO Core N-1/N final-header signature: `header=0`, message/command
  identity is unavailable, and no multipart assembly was observed. A later
  correlation follow-up identified the separate sequence-number/terminator
  collision responsible for these exact three-byte discards; see the F7
  framing follow-up below.
- `multipart_discard`: **0**; incomplete multipart assembly count: **0**. Every
  multipart assembly that actually started completed. Valid partial-final-frame
  headers without completion: **0**. No NEO Core 16/17-with-valid-final-header
  signature was observed in the captured trace.
- Ingress busy/full drops: **0**; trace-ring losses: **0**; since-boot ingress
  queue high-water mark: **16**. No disconnect event occurred in the captured
  window; the later since-boot diagnostic showed one disconnect/reconnect,
  which is not time-attributable to this run.
- No wrong-target authoritative confirmation or explicit controller action
  failure was logged. The test was manually stopped with action ID 259 (target
  6) in flight before its number confirmation appeared in the saved trace; a
  subsequent status showed preset 2. Therefore final-target correctness for
  that interrupted action was **not verified** and the soak does **not** pass
  the 500-action acceptance gate.

The Spark 2 trace does not reproduce the NEO Core's valid partial-final-header
followed by a missing frame remainder. It instead shows missing full-sync
readiness after dispatched queries, including one query with no valid frame
and one complete multipart response without a matching `Ready`. This is a
reliability failure requiring diagnosis; preserve the trace and do not tune
transport, parser, queue, deadline, retry, or controller behavior from this
  run alone. No firmware was changed during the attempt.

## F7 sequence-number framing fix and post-reconnect verification (2026-10-06)

The follow-up correlation confirmed that actions 95 and 179 had full-preset
queries with message number 247 (`0xF7`), each followed by 17 `invalid_wire`
three-byte discards. `SparkReceiveFrames::accept()` had treated the sequence
byte as the terminator, cleared the partial buffer at `F0 01 F7`, and discarded
the actual frame remainder. The frame reader now waits for the minimum seven
wire bytes before treating `F7` as a possible terminator. The trace and normal
host regressions demonstrate the previous 3-byte discard and cover sequence
`0xF7`, multipart preset assembly, split/coalesced input, checksum/7-bit
validation, resynchronization, short-frame reset, and message-number wrap.
Normal and trace PlatformIO builds passed; the trace firmware was flashed.

The operator reported that Spark 2 was physically powered off during this
validation, then powered it back on. Treat the captures as separate link
segments; do not combine them into one continuous soak.
`SparkDataControl::resetStatus()` resets `nextMessageNum` to `0x01` when a BLE
reconnect request is consumed. A subsequent controller boot trace began at
action ID 1 and showed the initial command/number/full-query messages 10/11/12
after startup traffic. A USB-UART controller reset was also observed while
reopening serial, so the captured message reset cannot be attributed solely to
the physical amp reconnect.

In the post-power-on Spark 2 capture at
`/tmp/opencode/spark2_after_reconnect_q247_proof.log`, action 203 sent full
query message 247. All valid frames for parts 0–16 completed and the assembler
reported `expected=17 received=17`; there was no three-byte invalid-wire
discard burst. But action 203 logged `full_data_timeout` at 2412 ms. A retry
queried message 249, whose full response matched and reached `Ready`. Thus the
framing collision is fixed at frame/multipart assembly, while this run did not
show a matching `Ready` on the first message-247 query. This remains an
application-level timeout/recovery observation for review, not evidence that
the framing fix explains the separate soak actions 131 (`full_query sent=0`)
or 239 (17/17 multipart assembly but no matching Ready). Those two earlier
cases remain unresolved. No deadline, retry, parser, queue, transport, or
controller behavior was tuned, and the 500-action soak was not restarted.

### Full-preset handoff follow-up (2026-10-07)

Trace-only handoff events were added and a six-change focused Spark 2 run was
captured at `/tmp/opencode/spark2_preset_handoff_trace_final2.log`. It showed an
expected stale-response case: startup full query msg=8 was revoked at t=7479
when a newer target was accepted; its response later completed multipart at
t=8473 and parsed at t=8483, but the t=8484 apply gate had `expected=0` and
rejected it without advancing the observation. A current query msg=14 later
passed the matching gate at t=10323, published the observation, released its
lane, and reached Ready at t=10328. Existing
`tools/test_preset_orchestration.cpp` covers rejecting
an obsolete full response after a newer intent. No production acceptance or
retry behavior changed.

This confirms one legitimate way a 17/17 parsed response is not authoritative;
it does not explain action 239 or action 203/msg=247, whose earlier captures do
not include handoff events. Action 131 remains unresolved and was not pursued.
The physical Spark 2 power-off remains a separate unresolved observation. The
500-action soak remains stopped for review.

## Spark 2 preset + FX core control acceptance (complete, 2026-10-08)

The earlier interrupted preset and FX attempts above remain historical failures;
they were not retroactively counted as passes. Separate fresh acceptance runs
subsequently completed:

- Presets: **500/500 accepted changing actions / 500 matching Ready actions**;
  details in `docs/SPARK_RECEIVE_FRAMING.md` (2026-10-07).
- FX: **500/500 dispatched / 500 controller-confirmed / 500 post-action Ready**;
  details and complete metrics in `docs/PANELAN_FX_DIAGNOSTIC.md` (2026-10-08).

These results complete the Spark 2 preset + FX core control reliability gate.
They do not cover tuner, looper, UI, or NEO Core follow-up work.
