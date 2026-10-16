# PanelLan SPI controller startup capture

## Opt-in I2C/SPI A/B variants

The normal `panelan-lvgl-controller` has **no** `PANELAN_I2C_AB_STAGE` flag.
Build one image at a time and flash only when ready to capture on hardware:

```sh
pio run -e panelan-lvgl-controller-i2c-ab-1
pio run -e panelan-lvgl-controller-i2c-ab-1 -t upload --upload-port /dev/ttyACM0
pio run -e panelan-lvgl-controller-i2c-ab-2
pio run -e panelan-lvgl-controller-i2c-ab-2 -t upload --upload-port /dev/ttyACM0
pio run -e panelan-lvgl-controller-i2c-ab-3
pio run -e panelan-lvgl-controller-i2c-ab-3 -t upload --upload-port /dev/ttyACM0
pio run -e panelan-lvgl-controller-i2c-ab-4
pio run -e panelan-lvgl-controller-i2c-ab-4 -t upload --upload-port /dev/ttyACM0
```

Use the matching env with `pio device monitor -e <env> --port /dev/ttyACM0
--baud 115200`, then reset with the monitor already attached. Substitute the
actual port. Uploader-induced reset is **not** a cold boot: for cold-start
comparisons remove/reapply board and external mini power as appropriate, then
start capture before resetting; record reset reason and power sequence.

Stage 1 (`PANELAN_I2C_AB_STAGE=1`) adds an explicit OLATB=0x7f all-CS-high
write after MCP init/shared reset and before first mini SPI selection; a failed
write stops mini initialization on that boot. Stage 2 (`=2`) performs a normal
`deselect()` immediately after each TFT's `tft.init()` returns, before rotation
and splash; it reselects that TFT before drawing. Failure of either operation
aborts that panel attempt and takes the existing 1-second init retry path,
with a best-effort all-high cleanup on failure. This reproduction env explicitly
uses legacy SPI2 even though the normal firmware uses SPI3. Stage 3 (`=3`) first probes
without a panel selected or initialized: it requests all CS high via MCP, then
initializes a separate LovyanGFX SPI2 bus (mode 0, 10 MHz, MOSI 10, SCLK 11,
MISO -1, DC 14). It probes I2C after bus init, after an empty SPI lock/config
transaction (no clocks), after one unaddressed command byte 0x00, and after
`bus.release()`. This separates those lifecycle steps. If a precondition fails,
it skips the byte. The A/B stage-3 update path is paused after setup, so it does
not subsequently initialize/draw minis; the main controller and MCP switch reads
remain active.
Stage 4 (`=4`) is a no-panel/no-CS control through the installed ESP-IDF SPI
master API instead of LovyanGFX. It mimics LovyanGFX's GPIO14 D/C setup and
high/low/high levels, with MCP all-high writes after each D/C transition; then
it checks SPI bus init, device add, a polling one-byte 0x00 transfer (10 MHz,
mode 0, no hardware CS), device removal/bus free, with MCP all-high writes
between phases. Return codes are recorded. The probe does not select, initialize,
or draw a TFT, and the A/B stage-4 update path is paused for the run. This compares LovyanGFX's
direct-register bus lifecycle with the ESP-IDF SPI driver while matching D/C
behavior. It still cannot rule out physical signal differences on the board.

Buffered `I2C/SPI A/B stage=N boundary=B success=0/1 display=D` records bracket
these actions: boundary 0=start, 1=high write (stages 1/3) or deselect (stage 2),
2=bus init (stage 3) or reselect (stage 2), 3=post-bus-init high write
(stage 3), 4=empty SPI transaction/post-check, 5=byte sent/wait/end (stage 3),
6=post-transfer high write before bus release, 7=bus release, 8=post-release
high write (stage 3, even if a precondition failed). Stage 4 uses 0=start,
1=pre-SPI all-high, 2=D/C output
setup, 3=post-setup all-high, 4=D/C low, 5=post-low all-high, 6=D/C restore,
7=post-restore all-high, 8=IDF bus init, 9=post-init all-high, 10=device add,
11=post-add all-high, 12=IDF transfer, 13=post-transfer all-high, 14=device
removal/bus free, 15=post-release all-high. The `detail` value carries raw
ESP-IDF/GPIO status values at operations that return them.
Boundary 4 has `success=0` when no byte was sent because
bus init or the post-init high check failed; no subsequent
boundary after an initial high-write failure means SPI was skipped. Stage 2
marks each display individually. Compare these
with existing `MCP write`, `MCP error`, `MCP sample` and `MCP intent` records;
the existing readback occurs only after a failed all-high write recovers on
retry, not on every A/B step. Even ACK and GPIOB readback cannot prove the
physical CS voltage at a TFT. Use a scope/logic analyzer for that question.
No serial printing occurs inside the SPI transfer.

## SPI host A/B result

Two uploader-started SPI3-host runs produced no MCP status-5 timeouts; both
completed all six live-card draws and connected to Spark. In the earlier normal
SPI2-host captures, every panel init was followed by an all-CS-high timeout.
The SPI3 A/B kept the same pin mapping, 10 MHz clock, mode, panel order, CS, and
reset behavior. Stage 3 localized failures to the LovyanGFX default-host bus
init/release boundaries: a no-clock transaction and a byte transfer, followed
by successful I2C probes, did not independently trigger the failure. Stage 4's
raw IDF bus/transfer with equivalent D/C transitions had successful probes.
On ESP32-S3, LovyanGFX's default SPI2 path restarts Arduino `SPI` with
`SPI.end()`/`SPI.begin()`; the non-default SPI3 path avoids that lifecycle. This
evidence prompted the normal mini backend to use SPI3.
These are software-reset tests, not cold boots, and do not prove cold-boot or
random blank-display reliability. See `PanelLanMiniSPI.h` and local captures
`logs/i2c-ab-stage5-spi3-20261015-070607.log` and
`logs/i2c-ab-stage5-spi3-20261015-070706.log` (captures are not committed).

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

## ESP-IDF 4.4.7 command wait diagnosis

The installed core bundles ESP-IDF 4.4.7. A temporary, target-write-only trace
inside its `esp32-hal-i2c.c::i2cWrite()` split the two internal intervals for
the OLATB release write: HAL bus-lock acquisition took 4-6 us, while
`i2c_master_cmd_begin()` took 999643-999700 us and returned 263 (`ESP_ERR_TIMEOUT`).
The temporary installed-core edit and project trace flag were reverted after this
diagnostic build; no permanent framework modification is part of the firmware.
The full startup trace is saved locally (not committed) at
`logs/i2c-core-lock-timing-20261015-055646.log`.

In ESP-IDF 4.4.7 `components/driver/i2c.c::i2c_master_cmd_begin()`, the command
mutex is acquired with the supplied tick timeout, then the command-event loop
computes remaining time but clamps `xQueueReceive()` to at least
`I2C_CMD_ALIVE_INTERVAL_TICK`, defined as `1000 / portTICK_PERIOD_MS` (one
second). If no ISR `ALIVE`/`DONE` event reaches that queue, the driver therefore
does not return at the configured 50 ms; it returns at approximately the
one-second liveness interval, runs `i2c_hw_fsm_reset()`, and returns
`ESP_ERR_TIMEOUT`. This matches the captured call duration. It identifies the
one-second wait precisely; it does not explain why that transaction produces
no I2C interrupt/event.

The normal mini SPI path uses SPI3 and GPIO10/11/14; the MCP uses I2C0/Wire on GPIO12/13.
LovyanGFX's Arduino SPI transaction path takes and releases its SPI host mutex;
that mutex is separate from the I2C0 locks. No shared lock, GPIO, or application
global-Wire caller tying the SPI transaction to I2C0 was found in the inspected
code. The repeated timing correlation remains real, but the reason no I2C event
arrives after splash rendering is unresolved. A stuck/loaded SDA or SCL, a
transient or peripheral/interrupt state issue remain possible; the GPIOB
readback is only after recovery and cannot distinguish them. Do not shorten the
driver's one-second liveness wait as a fix: that would only expose the timeout
earlier, not restore the missing I2C event. Capture SDA/SCL at the MCP during a
failed release before changing recovery, SPI, or reset behavior.
