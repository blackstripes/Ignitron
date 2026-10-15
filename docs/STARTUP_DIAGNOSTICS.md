# PanelLan SPI controller startup capture

Build with `pio run -e panelan-lvgl-controller`. With the board attached, start
the 115200-baud USB CDC monitor **before** resetting the board:

```sh
pio device monitor -e panelan-lvgl-controller --port /dev/ttyACM0 --baud 115200
```

Replace `/dev/ttyACM0` with the actual device path, then press RESET. Keep the
monitor open through the next reset to see the previous boot's breadcrumb.
The reset reason prints immediately after `Serial.begin`; USB CDC may drop early
lines if the host is not already listening. Startup never waits indefinitely
for a terminal (at most 1.5 seconds).

`[startup ... ms]` lines show entry/completion of display, Spark/BLE, MCP,
each TFT init/splash, first main UI update, first mini live-card update and
initialization. Each boundary reports free heap, lifetime minimum free heap,
and minimum remaining **current task** stack in bytes. A 10-second heartbeat
reports uptime, amp link state and the same memory readings, including while
disconnected. The previous breadcrumb is held in RTC no-init RAM with a marker
and checksum, not flash; `entered` without `complete` identifies the last
unfinished phase. It may be unavailable after power loss or if RTC memory is
not preserved by the reset. TFT progress is retained without printing while
the TFT is selected; pixel delivery is write-only and not verified by this log.
