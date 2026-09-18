# Codex Handoff

Work from branch: `feature/headless-serial-cli`

Full project plan: [`docs/PROJECT_PLAN.md`](docs/PROJECT_PLAN.md)

## Immediate checklist

- [ ] Build the current branch with the `esp32dev-headless` PlatformIO environment.
- [ ] Fix any compile/link errors.
- [ ] Verify headless startup does not require OLED, LEDs, or physical buttons.
- [ ] Review the serial CLI for blocking behavior and preserve normal BLE processing.
- [ ] Keep the existing Ignitron Spark protocol implementation intact unless a change is demonstrably required.
- [ ] Confirm serial commands for status, presets, banks, FX, tuner, tap tempo, and Spark 2 looper functions are wired to valid APIs.
- [ ] Leave hardware-dependent ESP32-S3 pin/display work deferred until the physical board arrives and can be positively identified.
- [ ] When hardware arrives, document the exact ESP32-S3 board, TFT/touch controllers, flash/PSRAM, exposed GPIO, and pinout source before implementing the board port.
- [ ] Bring up Spark 2 over direct APP-mode BLE before adding the custom display UI.
- [ ] After Spark 2 works, run the minimal NEO Core compatibility sequence in the project plan. Do not assume NEO Core support before testing.

## Final hardware direction

- ESP32-S3 2.8-inch touch-display board; exact board/pinout to verify on arrival.
- Six 0.96-inch ST7735S 80x160/160x80 SPI TFTs.
- Six primary momentary stomp switches.
- Two utility controls planned: `MODE` and `TAP/TUNER`.
- MCP23017 for switch inputs and/or display chip-select expansion.
- USB power first; battery later.
- Expression inputs later, likely ADS1115 + two TRS jacks, only after Spark parameter mapping is proven.

## Architecture guardrails

- Separate Spark BLE/protocol, controller state, input/actions, rendering, and hardware configuration.
- Keep the serial CLI in the final firmware as a debug/service interface.
- Avoid final pin assignments or enclosure dimensions until the actual modules are measured.
- Do not claim NEO Core or continuous wah support until verified on hardware.


## Current hardware/UI status — 2026-09-17

The original checklist above is partially stale. The authoritative current hardware status is in [docs/PANELAN_PROTOTYPE.md](docs/PANELAN_PROTOTYPE.md).

Confirmed now:
- PanelLan ZX2D80CE02S / SC05_X target builds on ESP32-S3.
- Main TFT and FT5x06 touch work in landscape.
- Direct BLE control has been verified on Spark NEO Core.
- Touchscreen hardware-preset switching works.
- Reconnection after a NEO Core power cycle works.

UI implementation target:
- Read [docs/UI_DESIGN.md](docs/UI_DESIGN.md) before changing the touchscreen UX.
- The visual concept is checked in at [docs/images/ignitron-ui-concept.jpg](docs/images/ignitron-ui-concept.jpg).
- Preserve protocol/UI separation and implement the UI incrementally.
- Next functional priorities are remembered-device selection, Spark 2 validation, and touchscreen FX/tuner/looper control before external displays/switches.


## Mandatory behavior specs

Before implementing or refactoring the custom controller UI, read these in order:

1. [docs/PANELAN_PROTOTYPE.md](docs/PANELAN_PROTOTYPE.md) — what is actually working on hardware.
2. [docs/STATE_MODEL.md](docs/STATE_MODEL.md) — canonical controller state and authority rules.
3. [docs/INTERACTION_SPEC.md](docs/INTERACTION_SPEC.md) — exact control semantics and transitions.
4. [docs/AMP_BEHAVIOR.md](docs/AMP_BEHAVIOR.md) — amp-native behavior, what not to reimplement, and hardware verification tests.
5. [docs/UI_DESIGN.md](docs/UI_DESIGN.md) — visual hierarchy and screen design.
6. [docs/PROJECT_PLAN.md](docs/PROJECT_PLAN.md) — larger hardware/product roadmap.

Do not implement behavior by guessing from the visual mockup.

Key rules:

- Spark-owned state is authoritative; local user actions create pending intent until confirmed.
- One shared action/state layer must serve touch, footswitches, mini TFTs, and serial CLI.
- Prefer one native Spark command over a sequence that imitates amp behavior.
- Spark 2 handles tuner mute itself. Do not turn FX off or manipulate volume to simulate tuner.
- Do not invent mutual exclusion across FX slots. Comp/Wah is one slot; other slots may coexist.
- Treat disconnect/reconnect as state invalidation and resync, not as continuity of old amp state.
- Record new hardware-observed behavior in AMP_BEHAVIOR.md before encoding assumptions.

Immediate software work should establish the state/action architecture and remembered-device behavior before scaling to external switches/displays.