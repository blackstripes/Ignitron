# Ignitron UI / UX Design Specification

This document is the implementation target for the custom PanelLan touchscreen UI and, later, the six per-switch mini displays.

![Ignitron UI concept](images/ignitron-ui-concept.jpg)

> The concept image is a visual direction, not pixel-perfect source artwork. The firmware must prioritize legibility, deterministic behavior, and the real 320x240 display constraints over reproducing every decorative detail.

## Design goal

Ignitron should feel like a purpose-built performance instrument rather than a generic embedded settings screen.

Primary UX principles:

1. **Glanceable from standing height.** Important state must be understandable in under a second.
2. **Performance-first.** Presets, effects, tuner, tap tempo, and looper are one-action destinations.
3. **State must be truthful.** UI should reflect state reported by the Spark, not merely the last command sent.
4. **No phone required while playing.** The Spark app remains useful for deep tone editing, but normal performance operation must be fully standalone.
5. **Touch is a complete fallback UI.** Everything required for testing and normal control should be accessible from the main touchscreen even before the external switches are installed.
6. **Physical switches eventually own immediate actions.** The main display provides context; mini TFTs tell the player what each footswitch does in the current mode.
7. **Dark stage-friendly visual design.** High contrast, restrained accent colors, minimal visual noise.

## Hardware constraints

Main display:

- PanelLan ZX2D80CE02S / SC05_X
- Logical landscape resolution: **320x240**
- ST7789 display through PanelLan/LovyanGFX
- FT5x06 capacitive touch
- No assumptions about anti-aliased fonts unless confirmed performant on the target

Do not design screens at desktop/mobile-app density. Text and controls must remain readable on a 2.8-inch display from several feet away.

## Visual language

The concept uses:

- Background: near-black / very dark charcoal
- Panels: dark gray, slightly lighter than background
- Primary text: white
- Secondary text: light gray
- Connected / positive / enabled: green
- Recording / destructive / drive: red
- Delay / Bluetooth / informational: blue or cyan
- Navigation / selected mode / warning: warm yellow or amber
- Disabled/unavailable: muted gray

Colors should always be reinforced by text, icon, border, or state shape. Do not make color the sole indicator.

### Typography hierarchy

At 320x240, use three practical tiers:

- **Hero:** preset name, tuner note, major looper state
- **Primary:** screen title, effect names, device names
- **Secondary:** bank/preset number, serial, BPM, bars, small status

Avoid tiny decorative copy. If a field cannot be read from standing height, either enlarge it or remove it from the performance screen.

## Persistent chrome

Most performance screens should share a compact top status bar:

- Connected Spark name/model
- BLE connection indicator
- Optional battery/status indicator later

Avoid putting clock/time on the controller unless there is a demonstrated reason; the concept mockup includes it only as visual filler.

A compact bottom navigation row may be used during the touchscreen-only prototype:

- PRESET
- FX
- LOOPER
- TUNER
- DEVICE

Once the physical MODE and TAP/TUNER controls are installed, revisit whether permanent bottom navigation is still necessary. Preserve screen area for performance information where possible.

## Screen 1 — Home / Preset

Purpose: answer “What sound am I on?” instantly.

Required information:

- Connection state
- Active preset name — visually dominant
- Bank and preset number
- Compact active-FX summary
- Current Spark identity if space permits

Touch behavior during prototype:

- Four large hardware-preset targets
- Optional bank previous/next controls after saved-bank behavior is verified
- Tapping FX summary may navigate to FX screen

Important: after a preset change, the UI should highlight the preset only after the command has been accepted/synchronized where possible. Avoid permanent “SENT” feedback that can be mistaken for authoritative state.

## Screen 2 — FX Control

Six large tiles:

- Gate
- Comp/Wah
- Drive
- Mod
- Delay
- Reverb

Each tile must show:

- Effect category/name
- ON/OFF state
- Strong selected state via fill/border/icon/text

Layout target: 3 columns x 2 rows.

Touch toggles the corresponding effect. The display must update from Spark state changes, including changes initiated from another controller/app if those messages are available.

Do not implement deep parameter editing in the first version.

## Screen 3 — Looper

This is a performance screen and should have the largest touch targets after the tuner.

Primary controls:

- REC / DUB
- PLAY / STOP
- UNDO / REDO
- CLEAR / DELETE (must be visually distinct and protected from accidental activation)

Status:

- Record/play/overdub state
- Number of bars
- BPM
- Straight/shuffle if available
- Click state
- Loop count / loop existence
- Current progress if protocol data makes it reliable

Destructive clear/delete should require either a long press, two-step confirmation, or physical long-press behavior. Do not make a casual single touchscreen tap erase a loop.

The Spark 2 internal looper is the primary target. NEO Core should not show unsupported controls.

## Screen 4 — Tuner

Full-screen, low-clutter performance display.

Required:

- Huge note name
- Sharp/flat indication
- Horizontal cents/offset scale centered at zero
- Numeric cents offset where useful
- Obvious “in tune” state
- Mute status if mute is supported/implemented

Target behavior:

- Enter quickly from TAP/TUNER long press and from touchscreen
- Exit quickly
- Avoid unrelated navigation clutter while tuning
- Use the largest practical typography on the device

## Screen 5 — Device Select

This is setup/connection UI, not a normal performance screen.

Show:

- Currently connected Spark
- Model
- Serial if known
- Remembered device
- Scan for devices
- Discovered candidates
- Explicit Connect / Pair / Select action

Required connection behavior:

1. Persist the last successfully selected Spark BLE address and identity.
2. Prefer it at startup/reconnect.
3. Do **not** silently switch to another nearby Spark if the remembered target is unavailable.
4. Offer Scan for Devices.
5. Require explicit confirmation before changing the remembered Spark.
6. Keep connected model clearly visible so the player always knows which amp/headphones are being controlled.

This is particularly important because the controller must support both Spark 2 and Spark NEO Core.

## Screen 6 — System / Settings

Keep settings shallow. Candidate items:

- Main display brightness
- Auto sleep / display timeout
- Performance/touch lock
- Boot to last-used mode
- Remembered-device management
- Firmware/build version
- BLE diagnostics
- Hardware diagnostics / touch test
- Serial CLI status

Do not turn this into a full tone editor.

## Performance / touch lock

Add an optional performance lock:

- Prevent accidental main-screen touches during playing
- Physical footswitches continue to operate
- A deliberate gesture or settings action unlocks touch

This becomes more important once the pedal is on the floor.

## External footswitch + mini TFT model

Planned physical layout:

- Six primary stomp switches with one ST7735S mini display above each
- MODE utility switch
- TAP/TUNER utility switch

The mini TFTs should answer only:

> “What will this switch do right now, and what is its current state?”

They should not duplicate the whole main display.

### Preset mode mini labels

```
P1       P2       P3
P4       BANK-    BANK+
```

Enhancement: replace P1/P2/etc. with abbreviated preset names once the UI can do so legibly.

### FX mode mini labels

```
GATE     COMP     DRIVE
MOD      DELAY    REVERB
```

ON effects should be visually bright/filled; OFF effects subdued.

### Looper mode mini labels

Tentative:

```
REC/DUB     PLAY/STOP    UNDO/REDO
CLEAR       STATUS       CONFIG
```

Final mapping must follow verified Spark 2 behavior and may change after real-use testing.

### Utility switches

**MODE**
- Short press: cycle Preset -> FX -> Looper -> Preset
- Avoid hidden long-presses for common mode changes

**TAP/TUNER**
- Short press: tap tempo
- Long press: tuner
- While tuner is active, short press may exit tuner if that proves intuitive

## Controller state architecture

Do not let individual screens query or own protocol state independently.

Create/maintain a controller-facing state model containing at least:

- BLE connection state
- Selected/remembered device identity
- Amp model
- Serial
- Active bank
- Active preset number
- Preset name
- FX names + on/off state
- Current performance mode
- Tuner note / cents / active state
- BPM
- Looper settings/state
- Mini-display labels/states
- Touch-lock state

Renderers should consume this state. Actions should request changes through a controller/action layer. Spark protocol code remains isolated.

## UI implementation guidance

The main PanelLan touchscreen should now use **LVGL 9.x** for the application UI, layered on top of the already-working PanelLan/LovyanGFX display and touch support.

Do **not** replace the proven board-specific ST7789/FT5x06 path with a generic display driver merely to adopt LVGL.

The six future ST7735S per-switch mini displays should remain lightweight direct-rendered displays driven from canonical controller state; they do not need their own LVGL UI stacks.

The detailed integration, buffering, tasking, component, build-target, and rollout plan is in [LVGL_ARCHITECTURE.md](LVGL_ARCHITECTURE.md).

Required architecture remains:

- `ControllerState`: normalized canonical state presented to all renderers
- `ControllerActions`: the only normal path from touch/footswitch/CLI intent to Spark actions
- LVGL main-screen renderer consumes state; widgets do not call Spark protocol code directly
- mini-display renderer consumes the same state
- BLE callbacks/protocol tasks never directly mutate LVGL widgets
- UI updates happen from one LVGL execution context
- use partial render buffers initially and measure memory/performance
- avoid blocking animations or redraw behavior that delays BLE processing

LVGL is a presentation framework, not a state-management system. STATE_MODEL.md and INTERACTION_SPEC.md remain authoritative.

## Touch sizing

On a 320x240 floor controller, touch targets should be large.

Guidelines:

- Prefer >= 44 logical pixels for important single-action targets where layout permits
- Leave visible gaps between destructive and adjacent controls
- Make tuner and looper controls especially large
- Debounce/touch-edge logic must send one command per intentional press

The existing preset touch handling already correctly prevents repeated commands from a held finger; preserve that behavior across all screens.

## Device capability awareness

The UI should adapt to the connected device.

Examples:

- Spark 2: expose internal looper controls once verified
- NEO Core: hide/disable Spark-2-only controls
- Do not show unsupported actions just because Ignitron has a method for them

A simple capability structure keyed from verified amp identity is preferable to scattered string comparisons throughout the UI.

## Immediate implementation sequence

### UI milestone A — navigation shell

- Add a screen enum/state machine
- Persistent compact connection header
- Touchscreen navigation between Preset / FX / Looper / Tuner / Device
- Keep current working preset buttons functional

### UI milestone B — trustworthy preset state

- Show current preset number/name from Spark
- Highlight the actual active preset
- Ensure reconnect refreshes state correctly

### UI milestone C — FX

- Implement 3x2 FX tile screen
- Read effect states
- Toggle from touch
- Sync UI from Spark responses

### UI milestone D — tuner

- Render live tuner note + cents
- Enter/exit tuner reliably
- Optimize for standing readability

### UI milestone E — Spark 2 looper

- Validate protocol on real Spark 2
- Implement only verified controls/states
- Add destructive-action protection

### UI milestone F — device selector

- Persist remembered BLE target
- Scan/list/select UI
- Explicit device switching between Spark 2 and NEO Core

Device selection is functionally important and may be implemented earlier than C-E if multiple nearby Spark devices interfere with testing.

### UI milestone G — settings/polish

- Brightness
- touch lock
- last-mode boot
- diagnostics
- visual refinement

### UI milestone H — external control surfaces

Only after main-screen behavior is stable:

1. Bring up one ST7735S mini TFT.
2. Add MCP23017.
3. Add one physical switch.
4. Prove mode-dependent label/state synchronization.
5. Scale to six mini displays and eight switches.

## Acceptance criteria for the main UI

Before moving heavily into enclosure work:

- Boot -> remembered Spark connection is understandable without serial logs
- Connection loss/reconnect is obvious
- Current device identity is trustworthy
- Preset selection and displayed preset state agree with the Spark
- FX state is readable and controllable
- Tuner is usable from standing height
- Spark 2 looper can be operated without looking at serial output
- Unsupported device features are hidden/disabled
- Accidental touch does not repeatedly fire commands
- UI remains responsive while BLE is scanning/reconnecting
- Normal performance does not require a phone or computer

## Visual concept source

The checked-in image under `docs/images/ignitron-ui-concept.jpg` is the current visual target. It shows conceptual versions of:

1. Home / Preset
2. FX Control
3. Looper
4. Tuner
5. Device Select
6. System / Settings

Use its hierarchy, spacing, dark-stage aesthetic, and state-color philosophy as inspiration. Adapt aggressively to the real 320x240 constraints instead of blindly copying the mockup.


## Behavioral implementation contract

The visual design in this file is intentionally subordinate to the detailed behavior specifications:

- [STATE_MODEL.md](STATE_MODEL.md) — canonical source of truth, pending/confirmed/stale semantics, device capabilities, reconnect invalidation.
- [INTERACTION_SPEC.md](INTERACTION_SPEC.md) — exact touchscreen/footswitch/CLI behavior, mode transitions, FX coexistence, tuner modality, looper destructive actions, failure handling.
- [AMP_BEHAVIOR.md](AMP_BEHAVIOR.md) — behaviors the Spark already owns, official product behavior, protocol facts, and hardware test matrix.

Codex must read those documents before implementing new controls. Do not infer interaction behavior from the concept image alone.

In particular:

- Spark-owned state must be confirmed by Spark; the UI may display a separate pending intention.
- unknown/stale/pending are not equivalent to OFF.
- different FX slots can coexist; Comp/Wah is one slot containing one current model.
- Spark 2 natively mutes output in tuner mode, so the controller must not disable every effect or alter preset state to create tuner mute.
- physical controls, touchscreen controls, and serial CLI must route through the same controller action/state path.
- external changes from the amp/app must update the same canonical state and all renderers.

## Main UI framework decision

The polished main-screen concept is now explicitly targeted at LVGL rather than hand-building a custom widget framework with raw LovyanGFX.

See [LVGL_ARCHITECTURE.md](LVGL_ARCHITECTURE.md) before implementing UI screens.

The visual concept remains the design target, but implementation should first prove LVGL display/touch bring-up and BLE coexistence, then establish ControllerState/ControllerActions, and only then expand screen-by-screen.