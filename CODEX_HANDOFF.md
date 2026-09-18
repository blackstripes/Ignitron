# Codex Handoff — Current Implementation Direction

Work from branch: `main`.

`main` is the authoritative development branch. New Codex and Astra sessions
must start from `main`. `feature/headless-serial-cli` is preserved as historical
branch history only; do not start new work there.

This file is the current starting point for a new Codex session. Older milestone text elsewhere may describe work that has already been completed; use the documents below as the authoritative current plan.

## Current known-good hardware/software state

Confirmed on physical hardware:

- PanelLan ZX2D80CE02S / SC05_X
- WT32-S3-WROVER / ESP32-S3
- 2.8-inch 240x320 ST7789 over 8-bit 8080 parallel
- FT5x06 capacitive touch
- landscape 320x240 UI coordinates
- PanelLan/LovyanGFX display/touch support works
- `panelan-sc05x` builds
- direct BLE connection to Spark NEO Core works
- touchscreen hardware-preset switching works
- NEO Core identity/serial can be displayed
- reconnect after NEO Core power cycle works
- serial/headless service path exists

Do not regress these checkpoints.

## Required reading order

Before changing architecture or UI, read:

1. [docs/PANELAN_PROTOTYPE.md](docs/PANELAN_PROTOTYPE.md)
2. [docs/STATE_MODEL.md](docs/STATE_MODEL.md)
3. [docs/INTERACTION_SPEC.md](docs/INTERACTION_SPEC.md)
4. [docs/AMP_BEHAVIOR.md](docs/AMP_BEHAVIOR.md)
5. [docs/UI_DESIGN.md](docs/UI_DESIGN.md)
6. [docs/LVGL_ARCHITECTURE.md](docs/LVGL_ARCHITECTURE.md)
7. [docs/ARCHITECTURE_REVIEW_NEXT_STEPS.md](docs/ARCHITECTURE_REVIEW_NEXT_STEPS.md)
8. [docs/PROJECT_PLAN.md](docs/PROJECT_PLAN.md)

Visual target:

- [docs/images/ignitron-ui-concept.jpg](docs/images/ignitron-ui-concept.jpg)

Do not implement behavior by guessing from the concept image.

## Main UI framework decision

Use **LVGL 9.x for the main 2.8-inch PanelLan touchscreen UI**.

Preserve the existing working PanelLan/LovyanGFX board support underneath it.

Do not:

- replace the board support with a generic SPI ST7789 implementation
- rewrite the FT5x06 driver
- put Spark protocol calls directly inside LVGL widgets
- run separate full LVGL stacks on the six future mini TFTs

The future six ST7735S switch displays should use a lightweight renderer fed by the same canonical ControllerState.

See [docs/LVGL_ARCHITECTURE.md](docs/LVGL_ARCHITECTURE.md) for the detailed port plan.

## Architecture contract

All normal inputs must use one path:

```text
touch / footswitch / serial CLI
              |
      ControllerActions
              |
      pending ControllerState
              |
       Spark protocol layer
              |
 response / ACK / notification
              |
     confirmed ControllerState
              |
  LVGL + mini display renderers
```

### State ownership

Spark owns:

- active preset
- effect model/state
- tuner active state + pitch
- Spark 2 looper state/settings
- tempo where reported
- device identity/capabilities

Controller owns:

- selected/remembered BLE device
- current view
- touch lock
- brightness/settings
- pending user intent

Unknown, stale, pending, unsupported, error, ON, and OFF are distinct UI states.

## Amp-native behavior rule

Prefer the smallest native Spark command and let the amp perform behavior it already owns.

Example:

- Spark 2 natively mutes itself in tuner mode.
- send tuner ON.
- do not turn six FX off.
- do not zero a volume.
- do not later replay FX states to restore sound.

Likewise:

- preset change is one native preset operation, not a series of effect mutations.
- FX toggle changes one current effect slot, not neighboring slots.
- tap tempo should start as one native shared amp tempo concept.
- looper actions should use native Spark 2 transport commands/status.

Record ambiguous real-device behavior in [docs/AMP_BEHAVIOR.md](docs/AMP_BEHAVIOR.md) before encoding assumptions.

## FX behavior contract

Performance slots:

1. Gate
2. Comp/Wah
3. Drive
4. Mod
5. Delay
6. Reverb

The Amp slot exists in the preset model but is not a normal performance bypass tile.

Different slots may coexist. Do not invent cross-slot mutual exclusion.

Comp/Wah is a single slot containing one current model. Toggling it bypasses/enables that model; it does not mean compressor and wah can both be independently enabled in the same slot.

## Immediate implementation task

Do **not** recreate all concept screens in one pass. Before the LVGL work below,
complete the architecture-correction gate in
[docs/ARCHITECTURE_REVIEW_NEXT_STEPS.md](docs/ARCHITECTURE_REVIEW_NEXT_STEPS.md).
In particular, do not treat the current legacy ACK/pending behavior as the
new controller's confirmation contract, and do not rely on the Arduino loop
as proof that protocol state has a single owner.

### Phase 1 — isolated LVGL bring-up

Create a separate PlatformIO target, suggested:

`panelan-lvgl-bringup`

Prove:

- LVGL 9.x initializes
- landscape rendering is correct
- FT5x06 touch mapping is correct
- touch does not repeat actions from a held finger
- partial-buffer rendering is stable
- free internal heap + PSRAM are logged
- board runs for several minutes without watchdog/reset

No Spark dependency is required in this target.

Pin the exact LVGL version after this is known-good.

### Phase 2 — LVGL + Spark BLE coexistence

Integrate LVGL with the existing PanelLan application while preserving current behavior.

Prove:

- NEO Core still scans/connects
- current identity display can be represented
- preset switching still works
- NEO power-cycle reconnect still works
- UI remains responsive during scan/connect/reconnect
- no watchdog/reset or obvious memory leak
- serial CLI remains available

Do not expand to all screens until this passes.

### Phase 3 — ControllerState / ControllerActions skeleton

Introduce the minimum canonical state/action architecture described in the specs.

Route the existing preset control through it.

Required first state:

- connection phase
- device identity
- current/confirmed preset
- pending preset
- stale/unknown semantics
- current controller view

Do not rewrite Spark protocol internals without demonstrated need.

### Phase 4 — first polished LVGL screen

Build the Home/Preset screen using reusable LVGL components:

- StatusHeader
- PerformanceTile
- StatusBadge
- navigation shell/theme

It must visually distinguish:

- confirmed
- pending
- unknown
- stale
- disconnected/reconnecting

Use the concept art as visual direction, not pixel-perfect layout.

### Stop point

After Phase 4, stop the broad feature expansion and document:

- exact dependency versions
- memory usage
- measured redraw responsiveness
- BLE behavior
- any architecture compromises
- any hardware assumptions still unverified

At that point perform an architecture review before implementing FX/Tuner/Device/Looper screens.

## Subsequent screen order

After the stop-point review:

1. FX
2. Tuner
3. Device selection
4. Spark 2 looper after real Spark 2 verification
5. Settings/diagnostics
6. external mini TFT + footswitch hardware

Device selection can move earlier if multiple nearby Spark devices interfere with testing.

## UI implementation rules

- Use reusable LVGL components and centralized semantic theme tokens.
- Keep fonts few and legible.
- Start with partial RGB565 buffers; measure before increasing.
- Avoid heap allocation in steady-state render loops.
- LVGL updates happen in one intentional UI context.
- BLE callbacks/tasks must not directly mutate LVGL objects.
- use short, interruptible animations only when they communicate state.
- do not use animation to imply success before Spark confirms.
- preserve one-touch-one-action behavior.

## Existing build targets

Do not break known-good environments while introducing LVGL.

Keep working where practical:

- classic ESP32 targets
- `esp32dev-headless`
- `panelan-display-bringup`
- current `panelan-sc05x`

Add LVGL as a separate target first. Move it into the normal PanelLan target only after display/touch + BLE coexistence is proven.

## Hardware not to implement yet

Do not add these during the LVGL architecture phase:

- six ST7735S mini displays
- MCP23017 switch expansion
- eight physical stomp inputs
- ADS1115 expression inputs
- enclosure-specific assumptions

Those come after the main UI/state architecture is stable.

## Model / agent workflow recommendation

For the operator running Codex:

- use **Sol High** for the initial LVGL integration, ControllerState/ControllerActions split, BLE/UI boundary, and first reusable UI architecture.
- use **Sol Medium** for individual screens and repetitive implementation after architecture stabilizes.
- use **Astra** as a review gate, not as the default implementation model.

Recommended Astra reviews:

1. after Codex proposes/implements the LVGL + state architecture but before broad screen implementation.
2. after Preset + FX + Tuner work on real hardware.

Ask Astra to focus on:

- state ownership violations
- BLE/UI race conditions
- LVGL lifecycle/threading hazards
- memory risk
- reconnect correctness
- unnecessary Spark-protocol rewrites
- guessed amp behavior
- capability gating

## Codex prompt to use

A new Codex session can be started with:

> Work on `main`. Read `CODEX_HANDOFF.md` and every document in its Required reading order before changing code. The next milestone is the LVGL architecture described in `docs/LVGL_ARCHITECTURE.md`: add an isolated LVGL bring-up target first, preserve the working PanelLan/LovyanGFX hardware path, prove display/touch, then prove LVGL + Spark BLE coexistence. Introduce ControllerState/ControllerActions before expanding the polished UI. Do not add mini TFTs, MCP23017, or rewrite Spark protocol. Respect the state semantics in STATE_MODEL.md and behavior rules in INTERACTION_SPEC.md/AMP_BEHAVIOR.md. Stop after the first polished Home/Preset screen and document versions, memory, BLE behavior, and remaining risks for review.

## Definition of success for this handoff

The next phase is successful when:

- LVGL runs reliably on the exact PanelLan hardware.
- touch works correctly in landscape.
- BLE connection/reconnect remains stable.
- current preset control still works.
- controller state has a clean authoritative/pending model.
- first polished Preset/Home screen uses reusable components.
- no mini-display hardware has been prematurely entangled.
- docs record what was measured rather than guessed.
