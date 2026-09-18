# Architecture Review — Required Next Steps

This document records the architecture/design review of
`feature/headless-serial-cli` at commit
`a393c24a9db5e089e68d5f143a2e6250c272c915`.

It is a gate for the next implementation phase. The project should **revise
the architecture contracts before integrated UI coding**, while allowing a
strictly isolated LVGL display/touch spike.

## Decision

The proposed main-display stack is sound:

- PanelLan ZX2D80CE02S / SC05_X
- WT32-S3-WROVER / ESP32-S3
- existing ST7789 8-bit 8080 and FT5x06 support through PanelLan/LovyanGFX
- LVGL 9.x for the main 320x240 touchscreen
- direct Spark BLE control
- lightweight direct rendering, not LVGL, for the six future ST7735S displays

Keep the existing board-specific PanelLan/LovyanGFX layer. Use a thin LVGL
adapter rather than replacing it with a generic SPI ST7789 or FT5x06 driver.

The blocker is not whether LVGL can render this UI. It is the inherited
protocol, state, and task boundary. The detailed state/interaction documents
describe a safer architecture than the current code actually provides.

## Critical corrections before integrated UI work

### 1. Establish explicit ownership and safe ingress

The existing code has concurrent access despite the Arduino-loop design:

- the NimBLE notification callback pushes into `std::queue<ByteVector>`;
- the main loop pops that queue;
- disconnect handling can reset queues/state;
- a background hardware-preset task also invokes shared protocol functions.

Relevant code includes `SparkDataControl.cpp` notification handling and
`SparkPresetControl.cpp` background task creation. An ordinary C++ queue and
read-only renderer access are not thread safety.

Adopt this ownership model:

```text
NimBLE callbacks       -> bounded, thread-safe raw-event ingress only
Touch / CLI / switches -> typed action queue only
                                  |
                         Controller/protocol owner
                         - parses protocol input
                         - owns mutable ControllerState
                         - owns connection lifecycle
                         - serializes Spark commands
                                  |
                          immutable state snapshot/revision
                                  |
                       Arduino UI loop, sole LVGL owner
```

BLE callbacks must never mutate `ControllerState` or LVGL objects directly.
The UI loop must never issue Spark protocol operations from individual widgets.

Use a bounded ingress queue and define queue-full behavior. Do not silently
drop a fragment and continue trusting an assembled protocol message.

### 2. Replace the legacy ACK/pending assumption

The current protocol code can promote a pending preset in response to an FX
ACK without sufficiently correlating the response to the initiating command.
`triggerCommand()` also uses mutable global command state that can collide
with background requests. The new state model must not reuse this behavior as
the definition of confirmation.

For every command, model these stages separately:

```text
queued -> sent -> transport ACK/accepted -> observed amp state
                                      \-> failed/uncertain
```

Rules:

- Pending is an overlay on last observed state, never the new confirmed state.
- Start response deadlines when a command is sent, not merely queued.
- Correlate evidence using connection epoch, action ID, protocol message
  number, operation, target, and expected value where the protocol permits.
- If a response cannot prove the requested value, query/refresh state instead.
- Initially allow only one Spark-mutating user action in flight globally.
  Expand concurrency only after correlation is demonstrably safe.
- On timeout, retain last-known state as uncertain and resync; do not present
  the previous state as freshly confirmed.

### 3. Add connection epochs and state revisions

Clearing cached state on disconnect is insufficient. On every reconnect or
device switch, increment a connection epoch and discard queued events, ACKs,
and actions from older epochs.

Add a preset/chain revision. An FX request must target the specific confirmed
preset/chain it was created for. A late reply for a former effect model must
not alter the newly loaded preset.

The owning controller context must reset protocol assembly, pending actions,
and authority of cached Spark-owned state. A BLE address match alone does not
prove that a Spark reboot preserved its current tone or looper state.

### 4. Keep blocking BLE work away from the UI-critical path

Current connection logic can run with a 30-second connection timeout. Async
scanning alone does not guarantee that connecting, service discovery, or
subscription will keep the UI responsive.

Either:

1. run serialized connection/GATT work in the controller/protocol owner away
   from the LVGL execution context; or
2. prove a non-blocking implementation with an instrumented coexistence test.

Do not add an LVGL task simply to hide an uncontrolled shared-state problem.
The existing looper timer's busy-spin behavior must be disabled from early
targets or converted to bounded work that yields.

### 5. Remove legacy policy side effects at the boundary

The new contract requires external Spark/App state to win. Existing tuner
handling can send a tuner-OFF command when tuner samples arrive outside the
legacy local submode, and legacy mode switching can send tuner commands in
response to tuner notifications. This can override externally initiated tuner
state or echo commands.

The controller must observe external tuner transitions and render them. It may
not manufacture a conflicting tuner transition to match a local view.

## LVGL port requirements

LVGL remains the recommended main-screen framework because the design needs
stateful tiles, capability-gated controls, pending/stale/unknown visuals,
device selection, and a tuner/looper interaction surface. It is not a state
management system.

Before the first integrated target:

- Pin exact versions of LVGL, PanelLan support, LovyanGFX, and the Espressif
  Arduino/PlatformIO platform. The current floating PanelLan Git dependency
  and compatible-range LovyanGFX dependency are not a reproducible baseline.
- Start with one partial RGB565 buffer, for example 320x20 or 320x40 pixels
  (12,800 or 25,600 bytes), rather than full-screen double buffering.
- Prefer internal DMA-capable memory for the initial display spike. Do not
  assume `BOARD_HAS_PSRAM` proves usable headroom.
- Define RGB565 byte order, dirty-rectangle bounds, stride, rotation owner,
  tick source, and buffer lifetime in the LVGL port.
- Call LVGL flush completion only after LovyanGFX no longer accesses the
  submitted buffer. Start with a measured synchronous flush; add asynchronous
  DMA or double buffers only when measurement justifies it.
- Log free internal heap, largest internal/DMA-capable allocation, PSRAM use,
  task-stack headroom, flush latency, and sustained-run watchdog results.
- Coalesce high-rate tuner samples while preserving ordered connection and
  control events.

Future mini TFT constraints:

- Do not use six LVGL stacks or preallocate six full 80x160 RGB565 sprites.
- Render only derived label/state data from canonical controller snapshots.
- When added, make SPI/CS ownership explicit: chip select must remain active
  until the relevant transfer completes, and MCP23017 polling must not race
  display-select updates.

## State and behavior clarifications

### Readiness and device identity

Define the minimum Ready state: verified device identity plus fresh current
preset data should be sufficient to enable preset control. Optional tuner or
looper fields cannot keep the controller in Syncing indefinitely.

Implement remembered-target enforcement in the first state slice, even if the
full device-selection screen is later. Never silently attach to the first
nearby Spark when the remembered target is unavailable.

Keep these concepts separate:

- active hardware preset;
- local bank browsing;
- saved/custom/uploaded preset identity;
- current preset/FX-chain revision.

### Capabilities and stale state

Capability support/confidence is distinct from current-state validity. Record
verification by model, firmware, operation, and evidence. A tuner can be
supported while its current state is unknown; that is not the same as an
unsupported tuner.

After reconnect, a fresh preset number does not prove that cached effect-chain
contents are fresh. Do not trust local preset files or cached hardware presets
as Spark authority during synchronization.

### FX, tuner, tempo, and looper

- The seven-slot model is appropriate. The six performance slots may coexist;
  Comp/Wah is one current model, and Amp is not a normal bypass tile.
- Retarget FX commands from fresh confirmed chain data, not stale effect names.
- Native Spark 2 tuner mute remains the correct policy: send tuner ON/OFF and
  do not alter FX, volume, or preset state to simulate muting.
- Add tuner entering/exiting timeout handling, sample expiry, external tuner
  transitions, and an explicit exit-uncertain state. No pitch samples do not
  prove tuner OFF. Verify that parsed offset units are cents before labeling
  them as such.
- The existing tap implementation locally calculates BPM and changes looper
  settings. It is not yet evidence that a safe shared native tempo command is
  being used. Verify emitted messages and delay/looper effects before making
  shared-tempo claims.
- Current looper status recovery is incomplete: a loop count cannot prove
  Playing versus Stopped after reconnect. Keep unobservable fields Unknown,
  gate actions individually, and distinguish tested protocol sequences from
  local assumptions.
- Test whether a Spark App and Ignitron can coexist and whether app-originated
  changes are delivered to the controller before promising external-change
  synchronization.

## Documentation maintenance

Before feature work, reconcile the following:

- `PROJECT_PLAN.md` still contains historical sections that call NEO Core
  unconfirmed despite the handoff/prototype recording successful preset control
  and reconnect testing.
- `PANELAN_PROTOTYPE.md` presents device selection as the next increment while
  other docs move it later. Keep the full screen later if desired, but retain
  remembered-target/identity enforcement in the initial controller slice.
- Multiple documents provide overlapping implementation order. Keep one
  canonical sequence and link to it from the remaining documents.
- Clarify that preserving legacy pending concepts does not preserve whole-preset
  ACK promotion.
- Name the concrete sole writer of controller state.
- Treat mini-display labels as renderer output, not a second state authority.
- Define TAP/TUNER timing precisely: long press must suppress short press, but
  tempo measurement must still have an unambiguous timestamp rule.

## Required implementation sequence

1. Revise state ownership, ingress, correlation, epoch, readiness, and timeout
   contracts in the state/interaction/architecture documents.
2. Pin display/UI dependencies.
3. Run the isolated LVGL display/touch spike below.
4. Establish safe bounded protocol ingress and a single controller/protocol
   owner; remove or isolate background request collisions.
5. Run LVGL + BLE coexistence with current preset control only.
6. Implement minimal connection/preset `ControllerState` and
   `ControllerActions`, including remembered-target enforcement.
7. Test touch/CLI equivalence, external preset changes, disconnect during a
   pending action, stale-reply rejection, and reconnect synchronization.
8. Implement the polished Home/Preset screen, then stop for review.
9. Add FX, verified tuner, device-selection polish, verified Spark 2 looper,
   and finally mini TFTs/footswitches.

Avoid a generic event framework, general action scheduler, extra UI tasks, or
comprehensive state domains before the first preset flow is correct.

## Required validation spikes

### LVGL display/touch spike

Create `panelan-lvgl-bringup` with one label/button and controlled dirty
rectangle updates. Validate landscape rotation, all touch corners, held touch,
drag-off behavior, repeated screen recreation, flush latency, memory, and
watchdog behavior during a sustained run. Use no Spark dependency.

**Initial checkpoint complete:** `panelan-lvgl-bringup` is pinned to LVGL 9.3.0
and was flashed to the PanelLan board. Its landscape display and LVGL button
touch path were verified with a single 320x40 RGB565 buffer. It logs memory at
startup and during steady state. The remaining corner, drag-off, sustained-run,
and flush-latency checks still apply before treating this as a fully qualified
production UI port.

### LVGL/BLE coexistence and protocol-correlation spike

`panelan-lvgl-controller` now provides the safe first half: a read-only LVGL
connection/identity card shares the ordinary Arduino loop with direct Spark
BLE, and the background preset-cache task is disabled so it cannot race the
future command owner. The PanelLan boots, serial CLI is available, and BLE
scanning was observed after flashing.

Next, add one preset action only after the canonical action boundary exists.
Instrument action ID, connection epoch, wire message number, ACK,
notification, and observed state. Exercise repeated Spark power cycles,
unavailable remembered targets, rapid requests, and delayed/lost response
injection. This validates bounded UI stalls, stale-event rejection, and the
real confirmation contract.

### Spark 2 behavior capture

Capture raw protocol traces for:

- tap tempo and its impact on delay/looper behavior;
- tuner entered/exited through the Spark itself;
- looper status after reconnect;
- simultaneous Spark App/controller connectivity and app-originated edits.

Record the results in `AMP_BEHAVIOR.md`; they determine truthful capabilities
and do not require building their final UI screens.
