# Spark Foot Controller Project Plan

This fork is being used as the software base for a standalone DIY foot controller for Positive Grid Spark amps, with the Spark 2 as the primary target. The goal is a polished, no-phone/no-computer playing experience while preserving Ignitron's proven Spark BLE/protocol work.

## Current status

Development branch: `feature/headless-serial-cli`

That branch contains an initial headless/serial-control path intended to let us exercise the Spark protocol without physical buttons, LEDs, or an OLED. It has **not yet been compile-tested on hardware** and should be treated as a starting point for Codex to review/fix rather than a finished implementation.

The upstream Ignitron code is built around classic ESP32 targets and uses NimBLE for Spark communication. The final controller hardware will likely be ESP32-S3-based, so there will be a deliberate port step rather than assuming the stock target is drop-in compatible.

## Product goal

Build a standalone floor controller that connects directly to a Spark 2 over BLE and provides:

- Preset/bank selection
- Individual FX toggles
- Spark 2 internal looper control
- Tuner UI
- Tap tempo
- Dynamic per-switch labels/status
- A larger contextual main display
- Optional expression pedal inputs later
- USB power first; battery power can be added later

The finished controller should not require the Spark mobile app for normal Spark 2 use.

## Hardware direction

### Main controller/display board

The intended prototype/final controller board is an ESP32-S3 2.8-inch color touch-display board purchased from Amazon. The board shown before ordering was marked approximately `ZX2D80CE02S V1.0` and appeared to be a 2.8-inch IPS TFT development board.

Do **not** hard-code the final pin map or display configuration until the physical board arrives and is positively identified. Verify:

- Exact ESP32-S3 module/flash/PSRAM configuration
- TFT controller and resolution/orientation
- Touch controller and bus
- Which GPIOs are actually exposed
- Existing onboard peripheral pin usage
- USB/serial behavior
- Available 3.3 V current

The current expectation is a roughly 240x320 IPS TFT, likely ST7789-family, but that is not yet considered proven.

### Per-switch displays

Six purchased modules:

- 0.96-inch IPS TFT
- 80x160 / 160x80
- ST7735S
- SPI
- 7-pin breakout
- 3.3 V

These are intended to sit above the six primary footswitches and dynamically show labels such as `P1`, `DELAY`, `REC/DUB`, `UNDO`, etc.

Plan to share SPI clock/data across all six displays and give each display independent selection. Candidate approaches:

1. MCP23017 drives six CS signals directly.
2. A 3-to-8 active-low decoder drives CS lines, with selection controlled either by native GPIO or the MCP23017.

Prefer the simplest reliable implementation after the final ESP32-S3 GPIO budget is known. The mini displays should be treated as write-only if possible; no MISO is expected to be required.

### Footswitches

Six purchased main switches:

- DaierTek PBS-24-302 style
- Momentary SPST
- 12 mm metal stomp switches

Current UX direction is **six primary switches plus two utility switches**:

- `MODE`
- `TAP / TUNER`

The exact utility-switch hardware can be selected later.

### GPIO expansion

A Waveshare MCP23017 breakout is planned for switch inputs and/or display chip selects.

Preferred switch electrical model:

- MCP23017 at 3.3 V
- Inputs use pull-ups
- Switches connect input to GND
- HIGH = idle, LOW = pressed

Interrupt support is optional; simple polling is acceptable initially.

### Power

USB 5 V is the initial power source.

Purchased 5 V -> 3.3 V mini buck modules can supply the six small TFTs and other 3.3 V peripherals. Verify actual output voltage with a meter before connecting displays. Do not assume the advertised current rating is a safe continuous rating without testing.

Add sensible local/bulk decoupling. Battery support is intentionally deferred until the USB-powered prototype is stable.

### Expression pedals — later phase

Possible future hardware:

- ADS1115 on I2C
- Two TRS expression jacks
- Series resistance/filtering/protection
- Tip/ring polarity configurable because expression pedals are not universal

Important: **continuous volume/wah control is not yet proven at the Spark protocol level.** Ignitron exposes parameter-change mechanisms, but the correct live parameter mapping still needs to be identified before expression support is promised.

## Physical UX direction

Use the Line 6 HX Stomp XL as inspiration for visual hierarchy and foot-control ergonomics, but keep the controller simpler.

Preferred layout: a **3 x 2 matrix of six primary switches** with a mini TFT above each switch, a larger main screen above the grid, and two dedicated utility switches.

Proposed behavior:

### Preset mode

```
P1       P2       P3
P4       BANK-    BANK+
```

### FX mode

```
GATE     COMP/WAH DRIVE
MOD      DELAY    REVERB
```

### Looper mode

Example only; final behavior should follow verified Spark 2 looper functions:

```
REC/DUB  PLAY/STOP  UNDO/REDO
CLEAR    STATUS/TAP CONFIG
```

`MODE` should cycle among the useful controller modes without relying heavily on long presses.

`TAP / TUNER` should preferably use:

- short press: tap tempo
- long press: tuner

The main display should show context rather than simply duplicate the mini labels: active bank/preset, preset name, connection state, active FX, looper state, tuner, and later expression state.

No rotary encoders are planned initially. The main screen is touch-capable and the Spark app remains available for deep tone editing. Add an encoder only if a concrete interaction later justifies it.

## Enclosure direction

Prefer a 3D-printed enclosure rather than a hand-cut metal faceplate.

Target a compact sloped enclosure that can fit on a Bambu P1P 256 x 256 mm build plate. A rough concept of 9-10 inches wide by 6-7 inches deep has been discussed, but **final CAD dimensions must wait for physical measurement of all boards and switches.**

Design considerations:

- PETG or ASA preferred over brittle PLA for the final unit
- Reinforced/ribbed top around stomp switches
- Heat-set inserts for serviceable assembly
- Recess/protect all displays so stomp loads never transfer into PCBs
- Keep wiring/service access practical

Before CAD, make a full-scale cardboard/paper layout and physically test foot spacing.

## Software architecture direction

The finished code should avoid mixing Spark protocol logic, UI rendering, and physical button handling.

Preferred separation:

1. **Spark transport/protocol layer**
   - Preserve Ignitron's working BLE/protocol implementation as much as possible.
   - Avoid unnecessary rewrites.

2. **Controller state layer**
   - Amp connected/disconnected
   - Amp identity
   - Bank/preset and preset name
   - FX state
   - Looper state/config
   - Tuner note/offset/state
   - Tap/BPM
   - Future expression state

3. **Input/action layer**
   - Footswitch events
   - Touch events
   - Serial CLI commands
   - Maps user actions onto protocol operations

4. **Renderer layer**
   - Main TFT renderer
   - Six mini-TFT renderer(s)
   - Headless/serial renderer for development/debugging

5. **Hardware abstraction/configuration**
   - Board pin definitions
   - Display bus/select implementation
   - MCP23017
   - Future ADS1115

Keep the serial CLI in the finished firmware as a development/service interface even after physical controls are working.

## Serial/headless development mode

The immediate development branch adds a first pass at headless serial control. Codex should compile-review it before assuming it works.

Desired serial commands include at least:

```
help
status
amp
refresh
preset 1
preset 2
preset 3
preset 4
bank up
bank down
fx gate toggle
fx comp toggle
fx drive toggle
fx mod toggle
fx delay toggle
fx reverb toggle
tuner on
tuner off
tap
loop rec
loop dub
loop play
loop stop
loop undo
loop redo
loop clear
```

The serial interface should return useful success/failure/status output and should not block the BLE processing loop.

Headless mode must not require an OLED, LEDs, or physical buttons to boot. Stock Ignitron display initialization can halt forever when the expected OLED is absent, so display initialization must be bypassed cleanly in this mode.

## Spark 2 functional target

The Spark 2 is the primary supported amp for this project.

First functional milestone after a successful BLE connection:

1. Read amp identity / serial / current preset state.
2. Change hardware preset.
3. Read/update FX state.
4. Enter/exit tuner and display tuner data.
5. Exercise Spark 2 internal looper commands.
6. Verify reconnect behavior after amp/controller restart.

The controller should use Ignitron's normal APP-mode/direct BLE path for Spark 2.

## Spark NEO Core investigation

Ignitron explicitly supports the original Spark NEO. NEO support was added before Spark NEO Core was released.

Positive Grid describes NEO and NEO Core as sharing the same amp modeling/effects/Spark app integration, while Core omits the NEO wireless transmitter feature. This makes NEO Core support plausible, but **it has not been confirmed in Ignitron and must not be treated as guaranteed.**

Once the controller can connect to Spark 2, perform a minimal NEO Core compatibility test:

1. BLE scan and identify advertised device/name/services.
2. Attempt Ignitron APP-mode connection.
3. Request serial/amp identity.
4. Request current preset.
5. Change preset.
6. Test one FX toggle.

If the NEO Core advertises a different name but uses the same Spark app protocol, add the smallest possible compatibility adjustment rather than creating a separate architecture.

## ESP32-S3 port constraints

The final controller is expected to use ESP32-S3.

Important differences from classic ESP32:

- S3 supports BLE but not Bluetooth Classic.
- Direct Spark APP-mode BLE is the path we care about.
- Ignitron features relying on Bluetooth Classic serial are not a requirement for this controller.
- Review ESP-specific MAC manipulation, BLE keyboard code, core pinning/task assumptions, filesystem configuration, and library versions during the port.

Do not entangle the first S3 bring-up with all displays and switches at once.

## Recommended development sequence for Codex

### Milestone 1 — stabilize the current headless branch

- Review the current `feature/headless-serial-cli` diff.
- Build `esp32dev-headless` with PlatformIO.
- Fix compile/link issues.
- Ensure headless boot does not touch OLED/button/LED hardware.
- Ensure BLE processing is never blocked by CLI input.
- Keep upstream behavior unchanged for normal/non-headless builds where practical.

### Milestone 2 — board arrival / hardware identification

- Run the vendor/demo firmware first.
- Confirm main TFT, touch, USB serial, flash/PSRAM.
- Record the exact board identifier and schematic/pinout source.
- Create a board-specific config instead of scattering GPIO numbers through the code.

### Milestone 3 — ESP32-S3 protocol bring-up

- Get the project compiling for the exact S3 board.
- Start with serial + BLE only.
- Scan for Spark 2.
- Connect and run the functional target sequence above.
- Do not start custom UI work until direct Spark control is stable.

### Milestone 4 — NEO Core compatibility test

- Run the minimal NEO Core test sequence.
- Document exact findings and any code differences required.

### Milestone 5 — main display UI

- Add a renderer for the 2.8-inch TFT.
- Start with connection/preset/FX/tuner/looper status.
- Keep the protocol layer independent from the graphics library.

### Milestone 6 — six mini displays + switch expander

- Bring up one ST7735S mini display first.
- Then prove shared SPI with multiple displays.
- Add MCP23017 switch inputs and display selection.
- Only after the electrical approach is stable should all six displays be connected.

### Milestone 7 — final interaction model

- Implement Preset / FX / Looper modes.
- Implement MODE and TAP/TUNER utilities.
- Minimize obscure long-press behavior.
- Make mini labels/state obvious from standing height.

### Milestone 8 — enclosure

- Measure actual hardware.
- Make a full-size physical layout mockup.
- CAD and print a prototype enclosure.
- Iterate ergonomics before final cosmetic print.

### Later milestone — expression inputs

- Add ADS1115/TRS hardware.
- Reverse-engineer/verify the relevant Spark live parameter mapping.
- Only then implement volume/wah behavior.

## Development rules / guardrails

- Preserve upstream Spark protocol code unless there is a demonstrated reason to change it.
- Do not claim NEO Core support until tested.
- Do not claim expression-wah support until the live parameter mapping is verified.
- Do not finalize board pins, enclosure dimensions, or power assumptions from Amazon photos alone.
- Prefer incremental hardware bring-up: one subsystem at a time.
- Keep serial diagnostics available throughout development.
- Maintain clear compile-time or board-specific separation so upstream classic ESP32 builds are not accidentally broken.

## Immediate Codex task

Start on `feature/headless-serial-cli`.

1. Build and inspect the current branch.
2. Fix the headless serial implementation until it compiles cleanly for the classic ESP32 `esp32dev-headless` environment.
3. Review the design for blocking calls, invalid assumptions, and accidental dependencies on the OLED/buttons/LEDs.
4. Do not begin the exact ESP32-S3 board port until the physical board/pinout is available.
5. Leave concise notes in the repository describing anything that still requires hardware verification.
