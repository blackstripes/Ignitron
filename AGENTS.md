# Project guidance

## PlatformIO

- Invoke PlatformIO through `PATH` (`pio` or `platformio`), not an absolute
  system path.
- Expected canonical command: `~/.local/bin/pio` symlinked to
  `~/.platformio/penv/bin/pio`; `~/.local/bin/platformio` points to the
  corresponding `platformio` executable.
- Never use `/usr/bin/pio` or the distro PlatformIO package; it is incompatible
  with the current system Python. Keep `~/.local/bin` before `/usr/bin` in
  `PATH` for interactive and login shells.
- Firmware environments are declared in `platformio.ini`. For PanelLan
  controller work, build the relevant `panelan-lvgl-controller` target; use
  `panelan-lvgl-controller-preset-trace` when collecting opt-in
  `PANELAN_PRESET_TRACE` diagnostics.

## Spark receive tracing

- `PANELAN_PRESET_TRACE` instrumentation is diagnostic-only. Keep the receive
  queue capacity, response-lane deadlines/retries, parser acceptance rules, and
  controller behavior unchanged when collecting transport evidence.
- Ingress callback events are buffered in a bounded ring and emitted from the
  controller task; do not add serial I/O to the NimBLE notification callback.
- See `docs/SPARK_RECEIVE_FRAMING.md` for trace event semantics and the limits
  of correlating split frames/multipart responses with their producing BLE
  notification.
