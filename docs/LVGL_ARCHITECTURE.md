# LVGL Architecture and UI Implementation Plan

> Status (2026-09-18): the LVGL controller target, display/touch bring-up,
> BLE coexistence, canonical preset/FX actions, and polished Home/FX/Tuner/
> Device screens are implemented on PanelLan hardware. The rollout sections
> below are retained as architecture rationale; current work starts with
> verified Spark 2 looper state/actions, not another LVGL bring-up.

This document defines the implementation path for the polished main touchscreen UI shown in the Ignitron concept artwork.

Read first:

1. [PANELAN_PROTOTYPE.md](PANELAN_PROTOTYPE.md)
2. [STATE_MODEL.md](STATE_MODEL.md)
3. [INTERACTION_SPEC.md](INTERACTION_SPEC.md)
4. [AMP_BEHAVIOR.md](AMP_BEHAVIOR.md)
5. [UI_DESIGN.md](UI_DESIGN.md)

The behavior specifications remain authoritative. LVGL is the presentation framework, not the source of controller state.

## Decision

Use **LVGL 9.x for the main 2.8-inch PanelLan touchscreen UI**.

Keep the six future ST7735S per-switch displays on a lightweight direct renderer rather than running LVGL on each mini display.

Architecture:

```text
Spark BLE / protocol
        |
ControllerState + ControllerActions
        |
  +-----+------------------------------+
  |                                    |
Main 2.8" PanelLan                 Six mini TFTs
LVGL UI                            simple direct renderer
  |                                    |
PanelLan / LovyanGFX                shared SPI
ST7789 + FT5x06                     ST7735S
```

## Why LVGL for the main display

The target UI needs more than primitive text and rectangles. It needs reusable components and many explicit visual states:

- confirmed / pending / stale / unknown / error
- consistent cards and tiles
- pressed/focused/disabled states
- modal tuner view
- destructive-action confirmation
- device scan/list/select UI
- settings lists
- reusable status badges
- consistent typography and spacing
- future transitions/animations without hand-building a GUI toolkit

Direct LovyanGFX remains useful below LVGL as the display/touch hardware layer.

## Why not LVGL for the six mini TFTs

The mini displays have a narrow purpose:

> Show what the associated footswitch does right now and whether that action/state is active.

They do not need lists, scrolling, modals, navigation stacks, or complex widget lifecycles.

Use a simple renderer driven by canonical ControllerState:

- label
- confirmed ON/OFF
- pending
- stale/unknown
- disabled
- current mode

This reduces memory, bus traffic, and complexity.

## Preserve the known-good PanelLan path

Do not replace the working PanelLan board support merely to adopt LVGL.

Current proven hardware path:

- PanelLan ZX2D80CE02S / SC05_X
- ESP32-S3 / WT32-S3-WROVER
- ST7789 main TFT over 8-bit 8080 parallel
- FT5x06 touch
- PanelLan Arduino support
- LovyanGFX
- landscape 320x240
- direct Spark BLE already working on NEO Core

LVGL should be layered on top of the known-good display/touch path.

## Integration options

Preferred order:

### Option A — existing supported LVGL/LovyanGFX bridge

If the exact pinned PanelLan/LovyanGFX stack exposes a clean, maintained LVGL integration that works with this board, use it.

Requirements:

- no regression in current display initialization
- touch coordinates remain correct in landscape
- no hidden blocking behavior
- no dependency on generic SPI ST7789 assumptions
- no replacement of the known-good PanelLan board config

### Option B — thin custom LVGL port

If the packaged bridge is awkward, write a very small adapter around the working PanelLan/LovyanGFX object:

- LVGL display flush callback writes the dirty rectangle through the existing display object.
- LVGL input callback reads FT5x06 through the existing touch API.
- keep rotation and coordinate translation in one place.
- no Spark logic in the adapter.

Do not rewrite the LCD or FT5x06 driver.

## Version strategy

Use LVGL **9.x**, but pin the exact version after a known-good hardware bring-up.

Do not leave an unconstrained latest-version dependency in PlatformIO.

The first successful build should record:

- LVGL exact version
- PanelLan library revision/version
- LovyanGFX version
- ESP32 Arduino/platform version
- buffer configuration
- measured free heap/PSRAM after startup

Once verified, those versions become the baseline until there is a reason to update.

## Render buffer strategy

Do not begin with a full-screen double-buffer assumption.

320 x 240 RGB565 full frame:

- 320 * 240 * 2 bytes = 153,600 bytes per frame
- two full buffers = 307,200 bytes before other UI/runtime allocations

The ESP32-S3 has PSRAM, but BLE, filesystem, strings, Spark preset data, LVGL objects, fonts, and future features also need memory.

Start with **partial RGB565 draw buffers**, for example enough for a modest number of horizontal lines. Measure performance before increasing them.

Requirements:

- UI must remain responsive.
- BLE processing must not starve.
- flush callback must return promptly.
- avoid per-frame heap allocation.
- log free internal heap and PSRAM during development.

Do not optimize blindly. Measure redraw time and BLE responsiveness on hardware.

## Threading / execution model

Treat LVGL as single-context UI code.

Rules:

- LVGL object creation/update occurs from one UI execution context.
- BLE callbacks/protocol parsing must not directly mutate LVGL widgets.
- BLE/protocol code updates ControllerState or queues events.
- UI loop consumes state changes and updates LVGL widgets.
- do not call LVGL from arbitrary BLE callbacks/tasks unless protected by an intentional LVGL locking design.
- no blocking animation or modal wait loops.

The simplest initial model is:

1. normal Arduino loop keeps Spark BLE/protocol processing alive.
2. ControllerState is updated centrally.
3. LVGL timer handler runs frequently enough for responsive UI.
4. renderer compares state revisions/dirty flags and updates affected widgets.

If a dedicated UI task is later required, add it only after the single-loop implementation is measured and shown insufficient.

## Controller architecture boundary

LVGL must not call SparkDataControl directly from individual widgets.

Required layering:

```text
LVGL event
   |
ControllerActions
   |
ControllerState pending intent
   |
Spark transport/protocol
   |
Spark response / ACK / notification
   |
ControllerState confirmed state
   |
LVGL render update
```

Same path is used by:

- touchscreen
- future footswitches
- serial CLI

This is required for interoperability and state consistency.

## Recommended source structure

Names can evolve, but keep the responsibilities separated.

Possible structure:

```text
src/
  controller/
    ControllerState.h/.cpp
    ControllerActions.h/.cpp
    DeviceCapabilities.h/.cpp
  ui/
    LvglPort.h/.cpp
    UiApp.h/.cpp
    UiTheme.h/.cpp
    UiComponents.h/.cpp
    screens/
      PresetScreen.*
      FxScreen.*
      LooperScreen.*
      TunerScreen.*
      DeviceScreen.*
      SettingsScreen.*
  hardware/
    PanelLanHardware.*
  minis/
    MiniDisplayRenderer.*
```

Do not force this exact directory layout if it fights PlatformIO/upstream structure. Preserve the separation even if filenames differ.

## Reusable LVGL components

Create shared components instead of styling every screen independently.

At minimum:

### StatusHeader

Shows:

- connection indicator
- current device model/name
- optional stale/reconnecting state

### PerformanceTile

Reusable for:

- FX
- preset targets
- looper actions

Supports states:

- confirmed ON/selected
- confirmed OFF
- pending
- unknown
- stale
- disabled
- destructive armed/error

### StatusBadge

Examples:

- CONNECTED
- RECONNECTING
- SYNCING
- PENDING
- LOCKED

### BottomNav

Touchscreen-prototype navigation only.

Should be removable/reducible later when physical MODE and TAP/TUNER switches exist.

### ConfirmOverlay / destructive state

For CLEAR/DELETE and future destructive settings.

Avoid a generic desktop dialog if a full-screen/tile confirmation is more legible at floor distance.

## Theme

Implement a centralized theme matching the visual concept.

Do not scatter literal colors/styles across screens.

Define semantic tokens:

- background
- surface
- surfaceRaised
- textPrimary
- textSecondary
- accentSuccess / confirmed
- accentInfo
- accentWarning / pending
- accentDanger / destructive/error
- disabled
- stale

The semantic meaning matters more than exact RGB values.

## Typography

Use a small, deliberate font set.

Target tiers:

- Hero: tuner note / preset name / major looper state
- Primary: screen titles and action labels
- Secondary: bank number, BPM, status metadata

Avoid shipping many font sizes/weights that waste flash and memory.

Before adding custom fonts:

- verify memory/flash impact.
- check standing-height readability on the actual 2.8-inch display.
- prefer fewer larger sizes over many tiny sizes.

## Animation policy

Animation is allowed only when it communicates state.

Good:

- short pending pulse
- compact transition between screens
- tuner needle smoothing
- brief confirmation/error state
- reconnect spinner

Avoid:

- decorative looping animations
- long screen transitions
- effects that make a control appear responsive before state is actually confirmed
- anything that blocks BLE or makes the controller feel slow

All animation durations should be short and interruptible.

## Screen implementation order

### Milestone LV0 — isolated LVGL hardware smoke test

Add a dedicated PlatformIO target, suggested name:

`panelan-lvgl-bringup`

**Completed on the PanelLan ZX2D80CE02S / SC05_X.** The checked-in target uses
LVGL 9.3.0, one 320x40 RGB565 partial buffer in internal memory, synchronous
LovyanGFX flushes, and the existing landscape FT5x06 path. Display rendering
and button touch interaction were verified on hardware. Startup telemetry
reported approximately 271 KB free internal heap, 263 KB DMA-capable heap,
and 2 MB free PSRAM. Re-measure these values after dependency or UI changes;
they are a baseline, not a capacity guarantee.

It must prove:

- LVGL initializes.
- screen renders in landscape.
- touch coordinates are correct.
- repeated touch does not generate unintended repeated control actions.
- display remains stable for several minutes.
- free heap/PSRAM are logged.
- no Spark code required for this target.

This is not the final app.

### Milestone LV1 — LVGL + BLE coexistence

Run LVGL with the existing Spark controller stack.

**Implementation started:** `panelan-lvgl-controller` pins the same LVGL
9.3.0/PanelLan stack and renders a read-only connection and device-identity
card from the normal Arduino loop. It also disables the legacy background
hardware-preset cache task for this target, because that task currently issues
independent Spark requests outside a canonical command owner. Boot and BLE
scanning and a live Spark connection were verified on the physical PanelLan.
Reconnect and sustained-touch runs remain required before this milestone
passes.

The target now contains one deliberately narrow action: hardware presets 1–4
go through `ControllerActions`, which permits one request only after a fresh
reported hardware-preset value is known. The old confirmed tile remains green,
the requested tile is amber while pending, and it turns green only after the
reported preset matches; conflict or a five-second timeout fails the request.
The first Home screen and navigation shell are now present: the Home screen
shows the confirmed Spark preset, its pending preset intent, and a compact FX
summary; the FX page is a live read-only 3x2 slot view. Tuner and looper
pages deliberately expose no Spark controls yet. Serial CLI commands have not
yet migrated to this action layer, so do not operate CLI commands that change
Spark state concurrently with this checkpoint.

For hardware UI regression checks, the PanelLan target accepts the development
only serial command `touch <x> <y>` and `screenshot`. The helper
`tools/capture_panelan_screenshot.py` can queue `--touch X Y` values and
`--command` values before a capture. This drives the normal LVGL input path,
not a separate widget test path. A complete-preset `refresh` is useful before
assessing FX content after reconnect, because transport connection and the
full active-preset payload can arrive at different times.

Prove:

- BLE scan/connect still works.
- NEO Core still connects.
- reconnect after NEO power cycle still works.
- LVGL remains responsive during scanning/reconnect.
- no watchdog resets.
- no material BLE regressions.

Do not build the polished UI until this checkpoint is stable.

### Milestone LV2 — canonical ControllerState / ControllerActions skeleton

Before adding multiple screens:

- introduce controller-facing state.
- route existing preset action through ControllerActions.
- expose confirmed/pending connection + preset state.
- keep serial CLI functional.
- do not rewrite Spark protocol unnecessarily.

This is the most important architecture milestone.

### Milestone LV3 — polished Preset/Home screen

Implement the visual language from the concept using reusable components.

Must show:

- connection state
- device identity
- actual confirmed preset
- pending preset separately
- preset name where available
- compact FX summary
- touch navigation

Do not fake unknown data.

**Initial implementation completed:** Home renders the connection/device card,
Spark confirmed hardware preset, pending selection feedback, and six compact
FX states. The bottom row changes between the touchscreen-prototype pages.

### Milestone LV4 — FX screen

3 x 2 reusable PerformanceTile grid.

Must honor:

- slot model
- Comp/Wah single-slot semantics
- pending/confirmed state
- external Spark/app changes

**Read-only state view completed:** the FX tab renders the six canonical
slots from `ControllerSnapshot`. Touch toggles stay intentionally absent until
FX requests have the same pending/confirmed controller-action semantics as
hardware-preset selection.
- stale/unknown states

### Milestone LV5 — Tuner

Implement large full-screen tuner.

Must:

- use native tuner command/state
- rely on Spark 2 native mute
- not rewrite FX states
- preserve/restore prior controller view
- render note/cents smoothly without hiding state truth

**External Spark 2 tuner observation checkpoint:** when canonical state sees
Spark's tuner active, LVGL auto-enters its read-only tuner view, hides normal
navigation, and restores the prior performance view when Spark exits. It shows
a large note and raw `OFFSET` only for a fresh parsed tuner sample; otherwise
it truthfully reports `LISTENING` / `NO FRESH PITCH DATA`. This path sends no
Spark command.

### Milestone LV6 — Device selection

Implement:

- remembered target
- reconnect to same target
- explicit scan
- candidate list
- select/confirm
- identity verification
- state invalidation when switching devices

This can move earlier if multiple nearby Spark devices interfere with testing.

### Milestone LV7 — Spark 2 looper

Only after real Spark 2 verification.

Implement native transport, state-driven labels, clear confirmation, and capability gating.

### Milestone LV8 — Settings / diagnostics / polish

Add only useful settings:

- brightness
- touch lock
- auto sleep
- firmware/build info
- diagnostics

Do not turn controller into a full tone editor.

### Milestone LV9 — external mini displays and footswitches

Only after main UI/state architecture is stable.

Use the mini-display renderer, not six LVGL instances.

## Build-target strategy

Keep the existing working targets intact.

Do not break:

- classic ESP32 environments
- `esp32dev-headless`
- `panelan-display-bringup`
- current `panelan-sc05x` baseline until replacement path is proven

Add a separate LVGL bring-up environment first.

Only merge LVGL into the normal PanelLan application target after smoke-test and BLE-coexistence checkpoints pass.

## Acceptance criteria before migrating fully to LVGL

The LVGL path is acceptable only if all are true:

- display initializes reliably on cold boot.
- touch mapping is correct in landscape.
- no periodic crash/watchdog reset.
- BLE still scans/connects/reconnects.
- NEO Core preset switching remains functional.
- UI remains responsive while BLE activity occurs.
- free memory remains healthy with room for later features.
- no direct UI-widget-to-SparkDataControl coupling is introduced.
- serial CLI still works.
- existing non-PanelLan builds remain buildable or intentionally isolated.

If these fail, fix the port before adding more screens.

## Historical model / agent execution strategy

Recommended development workflow:

### Sol High

Use for the architecture-heavy implementation:

- first LVGL integration
- ControllerState / ControllerActions split
- BLE/UI concurrency boundary
- device selection architecture
- first polished reusable component system

### Sol Medium

Use after the architecture is established for:

- individual screens
- repetitive widget/component work
- settings
- mini-display renderer
- tests and cleanup
- documentation updates

### Astra review gate 1 — before the large refactor

Have Astra review:

- STATE_MODEL.md
- INTERACTION_SPEC.md
- AMP_BEHAVIOR.md
- UI_DESIGN.md
- this LVGL architecture document
- Codex's proposed implementation plan/diff

Ask it specifically to find:

- state ownership violations
- race conditions
- unnecessary protocol rewrites
- LVGL lifecycle/threading hazards
- memory risks
- guessed amp behavior

Do not spend Astra cycles drawing individual widgets.

### Astra review gate 2 — after first integrated UI

After Preset + FX + Tuner work with real hardware, ask Astra to audit:

- reconnect behavior
- external Spark/app interoperability
- pending/confirmed correctness
- LVGL memory/object lifecycle
- concurrency
- capability gating
- whether any amp-native behavior was unnecessarily reimplemented

## Historical first Codex task

Codex should not immediately recreate all six concept screens.

First task:

1. Read all required docs.
2. Inspect current PanelLan display/touch implementation and PlatformIO config.
3. Propose a minimal LVGL 9.x integration that preserves the known-good PanelLan/LovyanGFX path.
4. Add a separate `panelan-lvgl-bringup` environment.
5. Prove display + touch with LVGL.
6. Measure/log memory.
7. Prove LVGL + existing BLE stack coexist without regressions.
8. Introduce the minimum ControllerState / ControllerActions skeleton needed to route the existing preset UI cleanly.
9. Stop and document results before implementing all remaining screens.

Do not:

- add mini TFTs yet.
- add MCP23017 yet.
- rewrite Spark protocol.
- invent new behavior not in the interaction/state specs.
- replace the PanelLan hardware path with a generic ST7789 implementation.
- couple LVGL widgets directly to SparkDataControl.

That implementation gate has passed; the active next phase is documented in
`CODEX_HANDOFF.md`.
