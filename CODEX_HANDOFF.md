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
- direct BLE connection to Spark NEO Core works
- touchscreen hardware-preset and confirmed FX switching works
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

The next product milestone is **verified Spark 2 internal looper control**.
Start with a protocol/capability probe, then add canonical looper state and
actions before enabling the current gated Looper screen. Do not claim looper
support from the touchscreen until its commands and state have been exercised
on Spark 2 hardware.

## Recommended next sequence

1. Probe Spark 2 looper commands/status through the existing serial CLI and
   record the result in `docs/AMP_BEHAVIOR.md`.
2. Add capability-gated `ControllerState`/`ControllerActions` looper flow;
   preserve pending/confirmed/stale semantics and destructive-clear handling.
3. Enable the Looper UI only for a verified capability and perform the T3–T5,
   T9–T10 hardware matrix scenarios.
4. Add tap-tempo through the same action boundary and verify its relationship
   to delay and looper tempo.
5. Exercise Spark-app interoperability, reconnect/resync, and rapid-command
   collision cases.
6. Only then bring up one mini TFT, shared SPI/MCP23017 inputs, and eventually
   the physical footswitch surface.

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

## Hardware not to implement yet

Do not add these until looper/state interoperability is stable:

- six ST7735S mini displays
- MCP23017 switch expansion
- eight physical stomp inputs
- ADS1115 expression inputs
- enclosure-specific assumptions

Those come after the main UI/state architecture is stable.

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

> Work on `main`. Read `CODEX_HANDOFF.md` and every document in its Required reading order before changing code. The PanelLan LVGL controller, Home/FX/Tuner/Device screens, canonical preset/FX actions, and Spark 2 tuner path are already implemented and hardware-tested. The next milestone is capability-gated Spark 2 internal looper control: first probe the existing protocol/CLI on hardware, record evidence, then add canonical state/actions and truthful UI. Preserve the PanelLan/LovyanGFX path, one LVGL owner, and the distinction between pending intent and Spark-confirmed state. Do not add mini TFTs, MCP23017, or expression hardware yet.

## Definition of success for this handoff

The next phase is successful when:

- Spark 2 looper capability, commands, and authoritative status are measured
  on hardware;
- looper actions share the same controller boundary as touch, CLI, and future
  footswitches;
- the Looper screen never presents an invented transport state;
- tuner/preset/FX behavior survives looper testing, reconnect, and external
  Spark/App changes; and
- no mini-display hardware has been prematurely entangled.
