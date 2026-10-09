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
- `panelan-lvgl-controller` builds and flashes
- mini display #1 (top-left) is integrated in the default controller target
  using hardware SPI2 and has been visually tested on the stable wall supply;
  it changes from `P1` on the Preset view to `GATE` on the FX view
- direct BLE connection to Spark NEO Core works
- touchscreen hardware-preset and confirmed FX switching works
- Spark 2 preset and FX core reliability each passed their 500-action acceptance
- Spark 2 identity/serial, external tuner observation, display entry/exit,
  fresh note/cents display, and native tuner mute behavior work
- reconnect after amp power cycle works
- serial/headless service, screenshot capture, and bounded serial observer
  paths exist

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
9. [docs/DEFERRED_FX_LATENCY_OPTIMIZATION.md](docs/DEFERRED_FX_LATENCY_OPTIMIZATION.md)

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

The user has explicitly frozen the current 2.8-inch touchscreen UI until all
six mini displays are working. After that hardware gate, use
`~/Desktop/Ignitron/miniUI.png`, `miniUI2.png`, and `miniUI3.png` to guide the
mini-display UI. Treat any 2.8-inch UI redesign as a separate later milestone;
do not modify it during six-panel bring-up.

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

## Current implementation status

The original LVGL bring-up, BLE coexistence, controller-state, and first-screen
checkpoints are complete in `panelan-lvgl-controller`. Do not repeat them as a
new feature effort.

Verified on the connected PanelLan/Spark hardware:

- polished Home/Preset, FX, Tuner, and Device screens render on the 320x240
  PanelLan display; touch and development touch injection work;
- preset and FX actions use `ControllerActions`; FX success is confirmed by
  fresh Spark-owned observations rather than a transport ACK alone;
- Spark 2 tuner can be entered from the display or externally, shows fresh
  note/cents data, preserves stable navigation labels, and exits to the
  selected destination; Spark performs its own tuner mute;
- Device shows Spark identity, amp serial, connection state, and tone-state
  freshness; and
- serial CLI/screenshot/serial-observer tooling remains available.

The next product milestone is **physical controller hardware integration**:
finish the six mini displays, switches, wiring, power/decoupling, and physical
integration. Keep the main 2.8-inch UI frozen until all six minis work. After
that hardware gate, resume verified Spark 2 looper control. FX confirmation
latency optimization is deferred until after hardware/UI bring-up; preserve its
evidence and checklist in `docs/DEFERRED_FX_LATENCY_OPTIMIZATION.md`.

## Recommended next sequence

1. Complete shared SPI and independent chip-select integration for all six mini
   TFTs; visually validate each panel.
2. Complete MCP23017/switch wiring and verify each physical switch maps to the
   intended mini display and action.
3. Verify power distribution, regulation, decoupling, wiring clearance, and
   operation with the physical controller integrated.
4. Keep the existing 2.8-inch UI frozen until all six minis are working; then
   build their UI from `~/Desktop/Ignitron/miniUI.png`, `miniUI2.png`, and
   `miniUI3.png`.
5. After the physical hardware gate, resume capability-gated Spark 2 looper
   commands/state/UI, then tap-tempo and Spark-app interoperability checks.

Spark 2 preset + FX core reliability acceptance is complete at 500/500 for each
surface. FX confirmation latency optimization is deliberately deferred until
after physical pedal hardware/UI bring-up; preserve its evidence and resume
checklist in `docs/DEFERRED_FX_LATENCY_OPTIMIZATION.md`.

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

Use `panelan-lvgl-controller` for the current touchscreen controller. Keep the
bring-up targets as diagnostics; do not fold unrelated hardware into the
controller target without a focused hardware checkpoint.

## Deferred beyond current hardware bring-up

The six mini displays, switches, wiring, power/decoupling, and physical
controller integration are the **current priority**, not deferred work. Keep
these other items parked until that hardware gate is complete:

- FX confirmation-latency optimization (see
  `docs/DEFERRED_FX_LATENCY_OPTIMIZATION.md`)
- ADS1115 expression inputs
- enclosure-specific assumptions pending physical measurement/fit
- looper and tap-tempo feature expansion until hardware integration is stable

Regardless of sequencing, preserve the user's explicit UI gate: do not redesign
the current 2.8-inch UI until all six mini displays are working.

## Review workflow recommendation

For the operator running Codex:

- use ordinary implementation/review capacity for bounded UI and controller
  work; keep protocol research evidence-based;
- use Astra for visual or unusually cross-cutting review, not as the default
  implementation path.

Recommended Astra reviews:

1. before enabling Spark 2 looper controls;
2. after the looper/physical-control integration changes task ownership or
   state boundaries.

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

> Work on `main`. Read `CODEX_HANDOFF.md` and every document in its Required reading order before changing code. Spark 2 preset and FX core reliability have passed 500/500 acceptance. The current priority is finishing the six mini displays, switches, wiring, power/decoupling, and physical controller integration. Keep the current 2.8-inch UI frozen until all six mini displays work. Do not resume the deferred FX latency optimization until after hardware/UI bring-up; see `docs/DEFERRED_FX_LATENCY_OPTIMIZATION.md`. After the physical hardware gate, resume capability-gated Spark 2 looper control. Preserve the PanelLan/LovyanGFX path, one LVGL owner, and the distinction between pending intent and Spark-confirmed state.

## Definition of success for this handoff

The current hardware phase is successful when:

- all six mini displays are wired and visually validated;
- physical switches and display chip-selects are wired and mapped correctly;
- power, regulation, decoupling, wiring clearance, and physical integration are
  stable; and
- the existing main 2.8-inch UI remains unchanged until that six-display gate
  passes.

Spark 2 preset + FX core reliability is already accepted at 500/500 for each
surface. Do not resume FX latency optimization, tuner/looper, UI redesign, or NEO
Core follow-up in this hardware bring-up task.
