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
their BLE writes succeed; a competing ordinary command returns false without
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

### Phase 6 - tune only after data exists

After the scheduler is stable, use hardware traces to revisit:

- 500 ms preset-number poll cadence
- two-second preset full-refresh retry cadence
- one-second FX query grace period
- five-second FX response window
- 15-second FX overall deadline
- Spark-2 80 ms BLE pacing

Do not shorten these merely to make the UI look faster. Tune from measured completion distributions.

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
