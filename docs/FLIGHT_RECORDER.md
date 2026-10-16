# PanelLan SPI-mini flight recorder

In the normal `panelan-lvgl-controller` profile, `flight status` reports the
pending RTC trace integrity and its detecting reset classification; `flight dump` prints
that snapshot and the current trace. Startup attempts replay **if** USB CDC is
open after the bounded wait. If no console was available, reopen it and use
`flight dump` before another reset. `log status|dump` still reads the separate
LittleFS event log; `boot-reset` is appended there after initialization (high
byte category: 0 unknown, 1 power-on, 2 external, 3 software, 4 panic,
5 interrupt WDT, 6 task WDT, 7 other WDT, 8 deep sleep, 9 brownout, 10 SDIO,
11 USB-UART chip, 12 USB-JTAG chip; low byte is `esp_reset_reason`). The normal
logger alone owns its rate-limited LittleFS writes. There is no erase command;
the only reset command is the explicitly marked test-only `flight test-restart`.

`FlightRing.h` layout v2: each ring has 64 x 16-byte fixed records plus 20-byte header
(1044 bytes). Current and pending rings plus version 2/CRC-checked 12-byte
pending metadata occupy 2100 bytes of RTC_NOINIT. Each record holds uint32 sequence and boot-relative
millis, uint16 value and boot ID, uint8 event and detail, and CRC16 (polynomial
0xA001, seed 0xffff) over the preceding 14 bytes. Header stores magic FLR1,
next sequence, layout version, boot ID, count, next write index, saturating
uint16 overwritten-record count, CRC16 over its first 18 bytes. Records
overwrite oldest after 64; the counter increments for each overwritten record
up to 65535 and resets on each boot. `flight status` and `flight dump` show
pending/current overwritten counts (zero for an invalid header). Boot ID wraps at 65535 and
sequence wraps at 32 bits (zero skipped).
The controller link map places all RTC_NOINIT data (including the independent
startup breadcrumb) at 0x858 = 2136 bytes in an 0x2000 = 8192-byte RTC slow
segment; the linker checks this bound.
Dump prints event codes as numbers:
1 reset (detail=IDF enum, value=ROM CPU0 | CPU1<<8), 2 stage (detail=stage,
value=0 enter/1 complete), 3 restart intent (detail=reset-mode flag), 4 scan
(1 start/2 match), 5 connect (1 attempt/0 GATT established/2 failed with
value=branch/3 subscribe failed/4 write failed), 6 link (0 down/1 up),
7 sync (value=phase), 8 preset and 9 FX (detail=0 send/1 confirm/2 fail,
value=slot), 10 parser (0 reject/2 inbound request; ordinary successes omitted), 11 ingress invalidated,
12 MCP failure (detail=status or fault), 13 task-create failure, 14 allocation
 failure, 15 invalid controller phase, 16 heap sample. Heap sample `value` packs
 free heap in the low byte and lifetime minimum free heap in the high byte;
 each byte is floor(bytes / 4096), saturated at 255 (255 means at least
  1020 KiB). Heap sample `detail` is the completed stage ID plus 0x20
  (see `StartupDiagnostics::Stage`). Stage-mark samples occur only on completion
  of Boot (0x20), MainDisplay (0x21), SparkInit (0x22), McpBegin (0x23),
  MainUiUpdate (0x30), MiniUiUpdate (0x31), and InitComplete (0x32). No stage
  entries or TFT init/splash transitions sample heap, though all stage events
  remain recorded. These milestone samples do not depend on Serial health output.
  `detail` 0x40 is the existing 10-second loop heartbeat, independent of USB
  CDC/Serial; 0x81/0x82/0x83 identify LooperTimer/HWpresets task-create and
  screenshot-buffer allocation failures. Failure samples follow their existing
  failure events. The 64-record ring overwrites oldest samples as it fills
  (including from other events); check `overwritten`
 in the pending/current header to detect loss. These are metadata only; no payloads,
 device identifiers or names are stored.
  The heap values are the aggregate reported by Arduino `ESP.getFreeHeap()` and
  `ESP.getMinFreeHeap()`, not the largest contiguous block or a PSRAM-only sample.
  Event 14 is emitted when a screenshot buffer request in SPIRAM fails; its
  associated sample reports internal/general heap only and cannot establish
  whether sufficient PSRAM was available.
 This change adds no RTC or static RAM fields: the existing 16-byte records
 and saturating overwrite counter are reused. In the normal controller build,
 `retained` remains 2100 bytes and the independent breadcrumb 16 bytes;
 the RTC slow segment remains 2136 bytes total. The build reports 157768 bytes
 RAM used out of 327680 (no additional static RAM for heap sampling).

At boot, only a CRC-validated ACK state and a valid prior current ring permit
copying that ring into the separate RTC pending ring before initializing the
new current ring. On fresh RTC state (no valid current or pending ring header,
no recognizable pending marker, and no valid ACK), boot establishes a validated
ACK for the next reset. An unknown or damaged pending/ACK marker with a valid
current ring, or with a valid pending ring header even when current is invalid,
is quarantined as `corrupt` rather than being interpreted as acknowledgment or
overwritten.
The first pending snapshot and the detecting boot's reset
category/IDF reason and raw ROM reasons for CPU0/CPU1 survive subsequent warm
resets without being replaced. `valid` means header and all counted records pass checks;
`partial` means header passes but at least one record/sequence/boot ID does not;
`corrupt` means unrecognized/damaged metadata, or a pending marker with invalid
version, metadata CRC, or ring header (quarantined across later boots, never
replayed or acknowledged); `unavailable` means a validated ACK state. Pending
metadata CRC covers one contiguous sequence of uint16 fields (marker low/high
halves, layout version, reset summary, raw ROM reasons, ring header CRC), without
header padding or XOR-combining CRCs. ACK uses the same CRC input with zero
summary, raw reasons, and header CRC, so it does not depend on stale pending data;
the header CRC covers its own fields. Older pending metadata CRCs are incompatible
with version 2 and remain quarantined rather than silently reinterpreted. Pending
metadata written by the earlier marker-excluding CRC also fails validation and
is quarantined even if its layout field is 2. Counted records have individual
CRCs. Invalid records are
explicitly labeled; they are not silently accepted. Current dump captures the
header once and checks each copied record's CRC, boot ID and expected sequence
for that header's slot. Concurrently overwritten or torn slots are labeled
`overwritten/torn/corrupt` rather than presented as part of the snapshot; it
does not freeze the ring while printing. A torn append may leave
a valid old header with an overwritten slot (partial), or an invalid header
(corrupt). Replay prints only a validated pending header, labeling bad records;
`partial` is still retrievable. Pending is acknowledged (validated ACK written) only
after all pending lines have been sent without reported Stream write failure and,
for Serial, while USB CDC remains open. Startup attempts this once if Serial is
open; `flight dump` also acknowledges on successful pending output. `flight status`
never acknowledges. The current ring is not cleared by acknowledgment; the
next boot can latch it as a new pending snapshot. If output is missed, interrupted,
or no Serial connection is present, pending remains available for later CLI dump
or a later boot's auto-replay. Neither a dump nor replay can guarantee the host
actually saved bytes already accepted by USB; capture the console output externally.
RTC_NOINIT is not flash: retention across power loss/brownout is
not guaranteed; corrupt metadata is not silently replaced.
The first reset is captured before the serial wait; the existing startup
breadcrumb remains independent. `millis` is relative to each boot, not a
wall-clock time. USB ROM reasons 21/22 are shown separately from IDF enums;
IDF `unknown` is not inferred to mean USB without the ROM value. Both CPU
raw reasons are reported, but they are not a proof of cause for an external
power event. The restart-intent record is written immediately before the
existing `ESP.restart()`; an unrelated software reset will not carry intent.
The opt-in CLI command `flight test-restart` records a `TEST` intent (detail
`0xFE`), prints and flushes a test marker, then calls `ESP.restart()`. Use it
only for an approved recorder test; it does not change Spark mode or send any
Spark transport commands.

The bundled ESP32-S3 Arduino 2.0.17 / IDF 4.4.7 `qio_qspi` sdkconfig already
enables flash ELF core dumps, CRC32, logging and boot-time core-dump check.
The selected `default_8MB.csv` includes a 64 KiB coredump partition at
0x7F0000. **Do not change or erase partitions or force a panic/brownout to
test it.** 64 KiB may be too small for a worst-case all-task ELF dump. Core
dumps normally require a panic; a brownout or USB reset alone won't create
one. For non-destructive extraction, first preserve the matching
`.pio/build/panelan-lvgl-controller/firmware.elf` (and firmware version),
then use an appropriate ESP-IDF 4.4 `espcoredump.py`/`esp-coredump` tool with
that exact ELF, or **read only** the 64 KiB flash region with esptool
`read_flash 0x7F0000 0x10000 core.bin` and decode the saved image offline.
Check the matching tool's help for raw-flash ELF core format flags; do not
run `erase_flash`, `write_flash`, uploads, or tools that clear the core after
reading without a separate explicit decision. An empty/invalid image does
not establish that no crash occurred.

 Host validation: `g++ -std=c++17 -Wall -Wextra -Werror tests/flight_ring.cpp -o /tmp/flight_ring_test && /tmp/flight_ring_test`,
 `g++ -std=c++17 -Wall -Wextra -Werror tests/flight_pending.cpp -o /tmp/flight_pending_test && /tmp/flight_pending_test`, and
 `g++ -std=c++17 -Wall -Wextra -Werror tests/flight_heap_sample.cpp -o /tmp/flight_heap_sample_test && /tmp/flight_heap_sample_test`.
A supervised retention check is: issue `flight dump`, issue
`flight test-restart`, then verify the marked restart intent, `ESP_RST_SW`,
previous event sequence, and pending acknowledgment after replay. No hardware
or core extraction was performed for this implementation.
