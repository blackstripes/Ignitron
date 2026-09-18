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
| Verified Spark | Spark NEO Core and Spark 2 (tuner path) |

The display is deliberately rotated to landscape in `PanelLanDisplay::begin()`.
LovyanGFX rotates touch coordinates along with the display, so touch targets
are defined in the same 320x240 coordinate system as the UI.

## Build and flash

Use the PanelLan-specific PlatformIO environment:

```sh
pio run -e panelan-lvgl-controller
pio run -e panelan-lvgl-controller -t upload
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

- LVGL player UI with Home/Preset, FX, gated Looper, Tuner, and Device screens.
- Preset and FX requests travel through `ControllerActions`; the visible value
  stays pending until a fresh Spark-owned observation confirms it.
- Spark 2 tuner supports display entry/exit and external observation, live
  note/cents rendering, native amp mute, and stable bottom navigation. A
  tuner-destination tap exits tuner before opening that destination.
- Device shows Spark model, amp serial, Bluetooth connection state, and tone
  freshness. Multi-device selection is still pending.
- On a Spark reboot, the BLE client refreshes GATT services and subscribes
  again before it considers the connection usable.

The original OLED, LEDs, and footswitch source files are excluded from this
target because their legacy GPIO initialization overlaps the PanelLan LCD bus.

## Deferred multi-device connection design

The desired experience supports both a Spark NEO Core and a Spark 2; the
controller must not be permanently locked to either one.

This remains a follow-up after looper/interoperability work:

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

## Next controller checkpoint

LVGL 9.x is now running above the known-good PanelLan/LovyanGFX path. Do not
replace that board support. The next controller checkpoint is verified Spark 2
internal-looper capability/state, followed by tap-tempo and interoperability
testing before external physical controls are added.
