# Mini preset-name recovery (2026-09-28)

## Contract and scope

The Preset-page label belongs to the mini's hardware slot, not the active
preset. Only top-left mini #1 is wired/configured today (IO10–14, SPI2); the
snapshot supports eight independent hardware-slot names. Other minis remain
future hardware work. Page-specific FX/utility behavior is unchanged. No edits
to the frozen 2.8-inch touchscreen renderer/layout were made in this fix.

Names are sourced only from the per-amp, checksum-validated hardware cache.
An active full-preset response is **not** a substitute for slot metadata. A
nonempty name remains last-known through cache invalidation/re-fetch on the
same identity/link. Disconnect or identity changes clear it. Unknown names
render as `UNKNOWN`, not `P1`. Serial identity reception immediately invalidates
the cache initialized before that serial was known, before re-querying model
and checksums.

## Findings

The previous snapshot-retention patch only preserved names already acquired in
RAM; after a flash/reboot it cannot restore `CLEAN` unless the hardware cache is
populated and validated again. The source also had two acquisition liveness defects:

* After three failed slot reads, the scheduler advanced without cancelling
  `pendingHWPresetSlot_`. `readHWPreset()` refuses a new read while that is set,
  so subsequent slots could all fail without issuing a request.
* After the bounded scan finished it never revisited missing slots. The
  prerequisite serial/checksum startup queries also had no idle recovery path.

The production scheduler now uses the host-testable `HardwarePresetScan`:
cancel on terminal timeout, three attempts per slot per scan, a 30-second
cooldown between scans, and idle-only sending. Metadata prerequisites retry
every five seconds while Ready. Existing response identity/link/slot/checksum
validation stays intact. Accepted/rejected cache replies have concise serial
diagnostics (no full preset payload or serial-number logging added).

The actual parser root cause was identified and fixed: NEO Core's extra
MessagePack float32 fields before the trailing preset checksum were mistaken
for the checksum, preventing cache validation. A
captured device run subsequently accepted hardware slots 1–4, and mini render
diagnostics showed `name=CLEAN`. LittleFS mount corruption remains unresolved;
neither visual behavior after switching presets nor cold-boot/reconnect recovery
was tested. Do not equate serial render diagnostics with visual verification.

## Validation

Host commands (from repository root; `/tmp/opencode` exists):

```sh
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc tools/test_hardware_preset_names.cpp -o /tmp/opencode/test_hardware_preset_names
/tmp/opencode/test_hardware_preset_names
g++ -std=c++11 -Wall -Wextra -Werror -Isrc tools/test_spark_message_sequence.cpp -o /tmp/opencode/test_spark_message_sequence
/tmp/opencode/test_spark_message_sequence
g++ -std=c++11 -Wall -Wextra -Werror -Isrc tools/test_panelan_mini_reference.cpp -o /tmp/opencode/test_panelan_mini_reference
/tmp/opencode/test_panelan_mini_reference
```

All passed. The new test exercises the actual production merge/lookup and scan
helpers: independent CLEAN/CRUNCH/eighth-slot names, empty/unvalidated cache,
same-link retention, replacement, identity/disconnect clearing, invalid slots,
final-timeout cancellation before the next slot, cooldown recovery, failed
sends, reset, and unsigned clock wrap.

Firmware build commands:

```sh
/tmp/opencode/platformio/bin/pio run -e panelan-lvgl-controller -e panelan-lvgl-controller-preset-trace
```

The normal SPI controller was flashed after the checksum fix; the captured
device diagnostics above came from that run. LittleFS corruption and visual
behavior still need separate validation.

Resolve LittleFS mount corruption; visually verify slot 1 CLEAN remains CLEAN
through selection of slot 2 CRUNCH and recovery after a missed slot read. Check
both a cold boot and reconnect.
`UNKNOWN` is expected while no validated name exists; a nonresponding amp cannot
be made to supply a name. Long/unsupported names retain existing SPI truncation
and ASCII substitution behavior. Additional physical minis are not validated.
