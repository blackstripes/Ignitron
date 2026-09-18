# PanelLan touchscreen controller prototype

This document records the known-good starting point for the custom Ignitron
foot controller. It is intentionally limited to the built-in PanelLan display,
touch input, and direct Spark BLE control. External footswitches and the six
small displays are a later hardware milestone.

## Hardware identified and tested

| Part | Confirmed detail |
| --- | --- |
| Panel/display board | PanelLan ZX2D80CE02S V1.0 / SC05_X |
| MCU module | WT32-S3-WROVER (ESP32-S3) |
| Main display | 2.8-inch, 240x320 ST7789 IPS TFT over 8-bit 8080 |
| Touch | FT5x06 capacitive touch |
| Firmware orientation | Landscape (320x240 logical pixels) |
| Board support | PanelLan Arduino library with LovyanGFX `BOARD_SC05_X` |
| Verified Spark | Spark NEO Core, serial `SHP11H15802392E` |

The display is deliberately rotated to landscape in `PanelLanDisplay::begin()`.
LovyanGFX rotates touch coordinates along with the display, so touch targets
are defined in the same 320x240 coordinate system as the UI.

## Build and flash

Use the PanelLan-specific PlatformIO environment:

```sh
pio run -e panelan-sc05x
pio run -e panelan-sc05x -t upload
```

It is configured for the ESP32-S3, 8 MB flash, QSPI PSRAM, and USB CDC. The
current development board is exposed on macOS as `/dev/cu.usbmodem1101`; update
the `upload_port` / `monitor_port` in `platformio.ini` if macOS assigns a
different device name.

For an isolated hardware check that excludes the Spark stack:

```sh
pio run -e panelan-display-bringup -t upload
```

Use a known-good USB data cable. Opening a serial monitor can reset the board
on this USB CDC setup, so the touchscreen is the preferred control surface for
normal Spark testing.

## Current UI and behavior

- Landscape player UI with a connection card and four large hardware-preset
  buttons.
- The connection card displays Spark model and serial number after the Spark
  identity handshake. This is the authoritative way to see which nearby Spark
  device is actually connected.
- A touch sends one preset command per tap; holding a finger down must not
  repeatedly send commands.
- On a Spark reboot, the BLE client refreshes GATT services and subscribes
  again before it considers the connection usable.

The original OLED, LEDs, and footswitch source files are excluded from this
target because their legacy GPIO initialization overlaps the PanelLan LCD bus.

## Multi-device connection design

The desired experience supports both a Spark NEO Core and a Spark 2; the
controller must not be permanently locked to either one.

The next BLE/UI increment will implement this flow:

1. Persist the last successfully connected Spark BLE address, together with
   its displayed model and serial.
2. At boot or reconnect, prefer that remembered address.
3. If it is unavailable, show an explicit `Scan for devices` action rather
   than silently choosing a different nearby Spark.
4. Show discovered candidates and require confirmation before replacing the
   remembered device.
5. Keep the connected model and serial visible at all times.

The current firmware still auto-selects the first nearby device advertising
the Spark BLE service. The identity card makes that behavior visible, but it
is not yet the final selection flow. Do not rely on it in a space with multiple
Spark devices until the remembered-device and confirmation UI is implemented.

## Deferred pedal hardware

The enclosure direction is an HX Stomp XL-style layout with up to eight
footswitches and six small per-switch displays. Do not assign expansion GPIO,
wire footswitches, or add the mini displays until the single-screen controller
and device-selection workflow are stable. The board's exposed IO and proposed
expansion approach remain documented in [PROJECT_PLAN.md](PROJECT_PLAN.md).


## Behavioral spec references

The current hardware prototype should now be evolved using:

- [STATE_MODEL.md](STATE_MODEL.md)
- [INTERACTION_SPEC.md](INTERACTION_SPEC.md)
- [AMP_BEHAVIOR.md](AMP_BEHAVIOR.md)
- [UI_DESIGN.md](UI_DESIGN.md)

The prototype's current direct preset switching is a proven transport/UI checkpoint, not the final state architecture. New controls should route through the shared controller-state/action model rather than adding more direct callbacks from individual UI widgets into SparkDataControl.

## Next UI framework checkpoint

The next main-screen implementation uses **LVGL 9.x** above the existing working PanelLan/LovyanGFX display/touch path.

Do not replace this known-good hardware support during the LVGL migration.

Follow [LVGL_ARCHITECTURE.md](LVGL_ARCHITECTURE.md):

1. isolated LVGL display/touch bring-up,
2. LVGL + Spark BLE coexistence,
3. canonical ControllerState/ControllerActions,
4. first polished Home/Preset screen,
5. review before broad screen expansion.

The current raw PanelLan UI remains the fallback/reference checkpoint until the LVGL path proves equivalent BLE/display/touch reliability.
