# Ignitron Controller State Model

This document defines the canonical runtime state for the custom Ignitron controller. It is the behavioral contract between Spark protocol handling, touchscreen UI, physical footswitches, mini TFTs, serial CLI, persistence, and reconnection logic.

The UI must not infer state independently. All renderers and controls consume or mutate one controller state model through shared controller actions.

## Core principle: one source of truth per kind of state

There are three classes of state.

1. Spark-owned state — authoritative state lives on the connected Spark device:
   - active preset
   - effect model and bypass/on-off state
   - tuner active state and tuner output
   - Spark 2 looper state/settings
   - Spark tempo/BPM where reported
   - device identity and device capabilities

2. Controller-owned state — authoritative state lives locally:
   - selected/remembered BLE device
   - current controller view
   - touch lock
   - brightness
   - UI preferences
   - pending user action metadata

3. Derived state — computed from Spark-owned + controller-owned state:
   - which controls are enabled
   - mini TFT labels
   - tile colors/badges
   - whether a control is pending, stale, unsupported, or destructive
   - which view should be visible

When Spark owns a value, the controller may show a temporary pending intention, but must not permanently overwrite the authoritative value until Spark confirms or a resync proves the new state.

## Recommended top-level model

Conceptually maintain:

- ConnectionState
- DeviceState
- CapabilityState
- NavigationState
- PresetState
- FxChainState
- TunerState
- TempoState
- LooperState
- UiState
- PendingActionState
- DiagnosticState

Use one long-lived state object. Rendering code should receive read-only access where practical.

## Connection state machine

Recommended phases:

- Disconnected
- Scanning
- Connecting
- Identifying
- Syncing
- Ready
- Reconnecting
- Error

Meaning:

- Disconnected: no active Spark BLE link.
- Scanning: BLE scan is running.
- Connecting: a target has been chosen and GATT connection is in progress.
- Identifying: link exists but model/serial/capabilities are not yet trustworthy.
- Syncing: identity is known; required current state is being refreshed.
- Ready: amp-owned state required for normal control is trustworthy enough for performance.
- Reconnecting: a previously-ready connection was lost and controller is trying to restore the same remembered target.
- Error: an operation failed in a way that requires visible intervention or retry.

### On disconnect

Immediately:

- mark all Spark-owned values stale, not false/off/zero.
- preserve last-known values for context, but render them as stale.
- clear or fail all pending commands.
- disable controls that would send Spark commands.
- keep controller-owned navigation, brightness, touch lock, and remembered-device identity.
- do not silently connect to a different Spark unless the user explicitly selects it.

### On reconnect

After BLE reconnect:

1. identify device again.
2. verify it matches the remembered target.
3. invalidate old Spark-owned state.
4. request/receive fresh current preset and required status.
5. derive FX state from the fresh preset.
6. enter Ready only after the minimum required state is trustworthy.

Never assume state survived a Spark reboot merely because the BLE address is the same.

## Device state

Track at least:

- BLE address
- model
- serial
- firmware if available
- identityKnown
- matchesRememberedDevice

Persist remembered BLE address plus last-known model/serial.

The BLE address is the primary connection target. Model/serial are confirmation and display identity.

Switching to a different device invalidates all Spark-owned state immediately.

## Capability state

Do not scatter model-name checks throughout the UI.

Normalize capabilities such as:

- hardwarePresets
- fxToggle
- tuner
- tapTempo
- internalLooper
- looperSettings
- inputVolume
- batteryStatus

Each capability should have a support confidence:

- Unknown
- Unsupported
- SupportedUnverified
- Verified

UI rules:

- Verified: normal control.
- SupportedUnverified: development/test only or clearly experimental.
- Unsupported: hide from performance UI.
- Unknown: do not present as working.

Do not infer a capability solely because a generic Ignitron method exists.

## Navigation vs amp state

Keep controller view separate from amp state.

Recommended views:

- Preset
- FX
- Looper
- Tuner
- Device
- Settings
- Diagnostics

Important distinction:

- Tuner view means what the controller is showing.
- tuner.active means whether Spark says tuner mode is active.
- Looper view is a control surface.
- looper transport is amp-owned state.

Track previousPerformanceView so tuner can temporarily supersede Preset/FX/Looper and return afterward.

## Preset state

Track:

- active hardware preset
- active bank
- active preset-in-bank
- preset name
- preset UUID
- known/stale flags
- optional pending hardware preset

### Authority

Spark response/current preset is authoritative.

A user tap on preset 3 creates a pending request for preset 3. It does not immediately make preset 3 authoritative.

### Pending presentation

While preset request is pending:

- old confirmed preset remains visibly current.
- requested preset gets amber pending treatment.
- do not show requested preset as solid confirmed green.
- when Spark confirms, pending becomes active and pending disappears.
- on timeout/failure, pending clears and old state remains until resync.

### Preset side effects

A preset change can replace the entire effect chain and preset tempo data.

Therefore a confirmed preset change must invalidate or refresh derived FX state rather than trying to preserve previous FX toggles locally.

Do not manually toggle effects to match the next preset. Spark owns loading the preset.

## FX chain state

Ignitron's preset model contains seven ordered slots:

1. Noise Gate
2. Compressor/Wah
3. Drive
4. Amp
5. Modulation/EQ
6. Delay
7. Reverb

The performance FX screen exposes six toggleable slots and omits Amp from normal bypass controls.

Per slot track:

- logical slot
- current effect model name
- enabled
- known
- stale
- optional pending desired enabled state

### Coexistence rules

Different slots are independent and may be enabled simultaneously unless Spark itself reports otherwise.

Examples that may coexist:

- Gate + Drive
- Drive + Delay + Reverb
- Comp/Wah slot + Mod + Delay
- all six performance slots on at once

The Comp/Wah slot is one slot, not two independent slots. A preset contains one current effect model in that slot. Initial controller behavior only toggles that current model's bypass state; it does not attempt to run compressor and wah simultaneously.

Changing effect model is different from toggling the slot and is out of scope for the first performance UI.

### FX pending lifecycle

On user toggle:

1. keep confirmed state authoritative.
2. set pending desired state.
3. send Spark effect on/off command.
4. render pending distinctly.
5. confirm on matching Spark response/ACK.
6. on failure/timeout, clear pending and resync preset/effect state.

Current Ignitron code already has pending/active concepts in SparkPresetControl; preserve the concept but expose it cleanly to the controller state model.

## Tuner state

Recommended phases:

- Off
- Entering
- On
- Exiting
- Unknown

Track:

- phase
- note
- cents/offset
- noteValid
- ampMutedByTuner as a verified capability/behavior flag, not a local mute command

### Spark 2 tuner behavior

Spark 2 itself mutes its output when tuner mode is entered. The controller should rely on that behavior and must not simulate tuner mute by disabling FX or changing preset state.

Entering tuner must not rewrite any FX slot state.

When tuner exits:

- return to previous performance view.
- keep last confirmed preset/FX state if still trustworthy.
- request/resync current preset if protocol/firmware behavior creates uncertainty.
- never restore FX by blindly sending a list of on/off commands.

See AMP_BEHAVIOR.md.

## Tempo state

Track one amp tempo concept initially:

- BPM
- known
- stale
- tap-sequence state
- last tap time

Spark 2 documents TAP tempo as affecting delay-related effects and looper tempo. Do not maintain separate local Delay BPM and Looper BPM unless hardware/protocol testing proves they are independently controlled.

The controller may calculate a candidate BPM from taps for immediate feedback, but amp-reported/confirmed value is authoritative when available.

## Looper state

Spark 2 internal looper state is amp-owned.

Recommended transport states:

- Unknown
- Empty
- Stopped
- Recording
- Playing
- Overdubbing

Track:

- support level
- transport
- loop count
- bars
- BPM
- click
- straight/shuffle
- undo/redo availability if known
- known/stale
- optional pending looper action

Do not synthesize confirmed looper state from button presses alone when Spark sends looper status/command messages.

If a command is sent but no confirmation arrives, show pending/unknown rather than pretending it succeeded.

## Pending action model

Every Spark command initiated by touch, footswitch, or serial should be representable as a pending action.

Track at least:

- action type
- unique id
- target
- desired value
- sent timestamp
- timeout

Rules:

- render pending separately from confirmed state.
- only one conflicting pending action per target at a time.
- second press on the same target while pending should normally be ignored.
- unrelated actions may proceed only if protocol serialization safely supports them.
- timeout produces visible error plus resync where practical.

## UI state

Controller-owned UI state includes:

- current view
- previous performance view
- touch lock
- brightness
- auto sleep
- diagnostics visibility

Current view must never be used as a substitute for actual amp state.

Example: user can be on FX view while looper is playing.

## Stale and unknown are not OFF

Never collapse:

- OFF
- UNKNOWN
- STALE
- UNSUPPORTED
- PENDING

Recommended visuals:

| Semantic state | Meaning | Visual treatment |
| --- | --- | --- |
| Confirmed ON | Spark says active | bright filled accent + ON |
| Confirmed OFF | Spark says bypassed | dark/subdued + OFF |
| Pending | command sent, awaiting confirmation | amber outline/pulse + pending mark |
| Unknown | no trustworthy value yet | neutral gray + ?/ellipsis |
| Stale | known before disconnect | dim last-known value + stale/link-loss badge |
| Unsupported | device cannot do this | hide in performance UI |
| Error | command failed | brief red error state; confirmed value remains |

## Event flow

Prefer a centralized event/reducer style even if implemented manually.

Examples:

- BleConnected
- BleDisconnected
- IdentityReceived
- PresetRequested
- PresetConfirmed
- FxToggleRequested
- FxStateReceived
- TunerOnReceived
- TunerOffReceived
- TunerSampleReceived
- LooperCommandReceived
- LooperStatusReceived
- TapTempoReceived
- CommandTimedOut
- ViewChanged
- TouchLockChanged

Protocol layer emits events. Controller actions emit request events. State changes happen centrally. Renderers observe state changes.

This prevents touchscreen, footswitches, and serial CLI from each maintaining their own version of reality.

## Persistence

Persist controller-owned state useful across boot:

- remembered Spark BLE address
- remembered model/serial
- brightness
- touch-lock preference if desired
- last performance view if desired

Do not persist amp-owned preset/FX/tuner/looper state as authoritative across sessions.

Cached preset files may remain useful internally, but fresh Spark state is authoritative after reconnect.

## Device switch

When user explicitly selects a different Spark:

1. mark current Spark-owned state stale.
2. disconnect old target.
3. clear pending actions.
4. connect selected target.
5. identify model/serial.
6. rebuild capabilities.
7. refresh preset/state.
8. enter Ready.
9. persist new target after successful identification.

Never carry FX/looper/tuner state from one Spark device into another.

## Acceptance tests

The state layer is not done until these pass:

- touchscreen preset change eventually agrees with mini/display state.
- external preset change updates controller without local press.
- FX toggle shows pending then confirmed.
- external FX toggle is reflected.
- disconnect with Delay ON makes Delay stale, not OFF.
- reconnect refreshes state before full Ready.
- entering tuner preserves FX state and uses amp-native mute.
- exiting tuner restores previous controller view without replaying FX commands.
- switching NEO Core to Spark 2 invalidates old device state.
- unsupported looper never appears as an active performance control.
