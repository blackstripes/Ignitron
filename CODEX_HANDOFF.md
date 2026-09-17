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
