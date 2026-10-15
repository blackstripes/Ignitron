# PanelLan SPI controller startup capture

Build with `pio run -e panelan-lvgl-controller`. With the board attached, start
the 115200-baud USB CDC monitor **before** resetting the board:

```sh
pio device monitor -e panelan-lvgl-controller --port /dev/ttyACM0 --baud 115200
```

Replace `/dev/ttyACM0` with the actual device path, then press RESET. Keep the
monitor open through the next reset to see the previous boot's breadcrumb.
Reset reason and previous breadcrumb are queued immediately after `Serial.begin`;
they replay from RAM when USB CDC is connected. Startup never waits indefinitely
for a terminal (at most 1.5 seconds). If the host connects after setup, the
controller loop drains the backlog before drawing the next mini frame. The
fixed 256-record ring discards new events when full and reports the number
dropped on the next connected drain; it is not persistent across resets.
The recorder in `src/StartupDiagnostics.{h,cpp}` and MCP call sites in
`src/PanelLanMiniMCP23017.h` enqueue these events without serial I/O; the
existing setup/loop drain in `src/Ignitron.ino` prints them outside mini SPI.

`[startup ... ms]` lines carry event-time timestamps (not replay time). They
include stage entry/completion, every MCP write's register/value/attempt/I2C
status (nonzero statuses retry once after Wire restart), requested OLATB/IODIRB
masks, reset low/high and elapsed 20/120 ms wait boundaries, per-TFT init
call/done, first splash begin/done, first live draw begin/done and their measured
durations, and retry/fault codes. MCP intent 0=OLATB preload before IODIRB
output enable, 1=IODIRB enable, 2=all CS high, 3=one CS low, 4=reset state,
5=final direction/output intent after reset wait;
faults 0=not ready, 1=invalid select, 2=overlap, 3=unexpected deselect,
4=select write failed, 5=deselect write failed, 6=setup write failed. Panel
phases 0=init call, 1=init done, 2=splash begin, 3=splash done, 4=live begin,
5=live done, 6=init retry, 7=draw retry. Splash/live draw durations end at the
renderer return, before the separate MCP CS-release transaction. `MiniUiUpdate` is an aggregate stage;
the per-display live phases provide exact boundaries. The connected-only
10-second heartbeat reports uptime, amp link state, free heap, lifetime minimum
free heap and remaining controller-task stack in bytes. The previous breadcrumb is held in RTC no-init RAM with a marker
and checksum, not flash; `entered` without `complete` identifies the last
unfinished phase. It may be unavailable after power loss or if RTC memory is
not preserved by the reset. TFT progress is retained without printing while
the TFT is selected; pixel delivery is write-only and not verified by this log.

`MCP write` records `before/after/elapsed` for `Wire.beginTransmission`, and
`secondBefore/secondAfter/secondElapsed` for `Wire.endTransmission` in wrapping
microseconds. The latter does **not** include the register/value buffer writes
between calls. `MCP retry request` precedes the next attempt. `MCP restart`
brackets `Wire.end()` (`status` is its bool return); `MCP delay(2)` brackets
the existing delay; `MCP Wire.begin(12,13)` brackets its bool return (`status`
0/1). `MCP switch read` records failed switch polling only (successful frequent
polls are omitted to protect the ring), with the existing no-STOP pointer
write followed by requestFrom (`attempt` field is the byte count). The separate
`MCP error` line records the register/value/attempt, I2C error code,
Wire.getClock() in Hz, Wire.getTimeOut() in ms, and the FreeRTOS current task's
name, handle, priority and **running core** (not affinity). Probe request
failures use synthetic codes 0xfd (no byte) / 0xfe (byte without available
data); these are not Wire.endTransmission codes.

Once per boot, only after the first failed *all-CS-high OLATB* write that
succeeds on its retry, three `MCP sample` lines read IODIRB (0x01), GPIOB
(0x13), and OLATB (0x15) in that order. `reg` identifies the sample,
`value` is the sampled byte when `attempt` (request byte count) is 1;
otherwise 0xff is a missing-sample sentinel, not pin data. `status` is the
register-pointer write status; `before/after` bound the pointer phase and
`secondBefore/secondAfter` bound requestFrom when performed. The read-only
probe uses a register-pointer write with STOP, then a one-byte request with
STOP; it does not use endTransmission(false) (which can retain TwoWire's lock)
or perform additional recovery/delays on failure. The probe waits until GPB
output directions have been configured, so a recovered initial input-latch
preload does not trigger it. Software intents
are **requested** states, whereas GPIOB is a sampled pin state and OLATB and
IODIRB are sampled register values; failed probes do not establish pin state.

On this ESP32 Arduino core, TwoWire::beginTransmission takes its TwoWire mutex
indefinitely. TwoWire::endTransmission invokes i2cWrite (ESP_ERR_TIMEOUT maps
to status 5). The HAL also takes a bus lock with portMAX_DELAY, then calls
i2c_master_cmd_begin with Wire's timeout (default 50 ms); Wire.begin with
frequency 0 defaults to 100 kHz. Consequently endTransmission elapsed time
includes possible HAL lock waiting **and** the transfer; these cannot be
separated without modifying the core, which this capture does not do. No other
project global Wire user is known; the main touch controller uses I2C_NUM_1
on GPIO8/9, while this MCP uses global Wire on GPIO12/13. Times are call-site
wall durations, not electrical measurements.

All masks and active-display tracking reflect **software requests / successful
I2C ACKs**, not electrical pin readback. An I2C ACK does not prove a CS or RES
voltage or a pixel arriving at the display. Overlap detection only observes
selection calls through this software path, not another SPI client, bus contention,
or wiring shorts. The high-CS intent is logged before the low-CS request;
failed writes may leave the real pin state unknown. Serial replay runs only in
setup/loop outside the mini SPI selection window; recording during drawing is
RAM-only. LovyanGFX's ST7735S initialization command table includes SWRESET
150 ms, SLPOUT 500 ms, NORON 10 ms and DISPON 100 ms delays (760 ms in total),
in addition to the external reset's 10 ms pre-low, 20 ms low and 120 ms high
waits, six sequential panel inits (at least 4560 ms of table delays), splash
drawing, the 5-second splash hold before live cards, and other startup work.
Backlight is externally powered and not measured or controlled here. Recorded
durations are wall time, not proof of electrical timing or on-screen brightness.

## Microsecond Wire timing capture

The captures `logs/mini-i2c-timing-20261014-220841.log` and
`logs/mini-i2c-timing-20261014-221255.log` each observed six failed all-CS-high
writes during startup. `beginTransmission()` took 7-11 us; each following
`endTransmission()` returned status 5 after 999708-999761 us.
`Wire.end()` took 73-83 us, the pre-existing 2 ms retry wait took about 1794 us,
`Wire.begin()` took 310-316 us, and the successful retry transaction took about
356-360 us. The measured configuration was 100 kHz and 50 ms timeout; the task
was `loopTask`, priority 1, running on core 1. Readback after the first recovered
release was IODIRB=0x80, GPIOB=0x7f, OLATB=0x7f.

The approximately one-second wait is inside `Wire.endTransmission()`. That
API call includes both the ESP32 HAL I2C bus-lock wait and the hardware
transaction; the application-level timestamps cannot divide those components.
The 50 ms configured timeout does not bound the HAL's `portMAX_DELAY` lock wait.
The current capture therefore locates the delay at the call boundary but does
not establish whether most time is spent acquiring the HAL lock, in the driver
transaction, or due to scheduling. The register readback is after recovery and
does not reveal line levels or a transient during the failed transfer.
