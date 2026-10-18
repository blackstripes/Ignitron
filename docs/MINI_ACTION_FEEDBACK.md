# Mini action feedback and timing

The SPI mini cards derive their appearance from `ControllerSnapshot` in the main
loop, after the LVGL update. A pending FX card displays the requested ON/OFF
direction (without changing Spark's confirmed `enabled` value) and a small
amber corner dot. Confirmation removes the dot without changing the ON/OFF
appearance. Failure, timeout, link loss and stale state continue to use Spark
observations; preset failures mark only the affected slot. The accepted pending
preset takes precedence over the old confirmed number: its mini card immediately
uses the same amber filled bar and waveform as number-confirmed Loading and
Ready, plus a small amber corner dot only while pending. Number proof removes
the dot without dimming the selection; failure restores the confirmed slot
with a brief red badge on the failed slot. Browsing another bank does not
change the global 1–8 target. Other slots are grey during a valid pending or
number-verified Loading transition; disconnected/unknown slots remain amber
outside that transition. Unchanged card keys are not sent to SPI; the target
and previously confirmed slot are serviced before other minis on the browsed bank.
An action failure leaves the fresh, known Spark ON/OFF or confirmed preset
selection visible with a small red corner badge on the affected card;
stale/unknown observations show no asserted ON/OFF, but retain the badge.
ControllerState expires failure badges
after 1.5 seconds in its refresh loop and clears the failed preset target at
the same time. Confirmed Spark values are not rewritten by this timer.
After matching post-switch hardware-number verification, the selected preset
card can show confirmed while the separately refreshed FX chain remains stale.
This link-scoped number proof is cleared on disconnect/new link and does not
make any FX slot known. A stale reconnect cannot retain that selected appearance.

The main LVGL preset summary and FX page use the same fresh/known pending
direction without modifying the confirmed controller value. Their amber corner
marker disappears on confirmation, leaving the requested ON/OFF body unchanged;
a failure uses the fresh observed direction with a red marker. Stale/unknown
cards say `?` or `SYNC` instead of asserting a direction, retaining a red
failure marker during resync when the action failed. The picker moves to
the pending preset. The large main card keeps its original dark cabinet artwork,
fonts and layout with no selection tint, corner dot or ordinary switch status.
During pending or locally verified Loading it shows a complete cached target
name, description and six FX states only when current-link checksums, slot
checksum, cache generation, amp name and serial all match and all six mapped
pedals have boolean on/off values. Otherwise it shows the target slot
name/fallback and unknown FX, never the old chain. Ready uses the actual Spark
snapshot; an identical preview requires no changed label/tile redraw. Preview
data is display-only and cannot authorize actions or affect the FX screen.
The cache preview borrows the validated slot synchronously and copies only
display fields; no full preset/JSON copy is made per frame. During a preset
Pending or locally number-verified Loading transition after that amp's link
has reached Ready, the main connection label stays CONNECTED rather than
showing ordinary switch progress as BLUETOOTH SYNCING. Initial link sync,
including an accepted pending preset before the first Ready, still shows
SYNCING. This presentation-only Ready history is cleared on link loss,
reconnecting or amp identity change; it does not affect controller readiness.
Failure returns to the last trustworthy Spark/stale presentation with brief
red/error feedback.
Validated slot names are cleared on disconnect
or amp identity change; no name is inferred from an old active preset. Unchanged
labels/styles do not request another dirty-area redraw of the FX icons.

The main-screen flash investigation found no reset/backlight/full-screen clear
in the panel flush path. It did find that the bundled LVGL 9.3
`lv_obj_set_local_style_prop()` always reaches `lv_obj_refresh_style()` and
invalidates its object, even for equal values; `lv_label_set_text_fmt()` also
explicitly invalidates. Revision-triggered renderers had called these setters
for all six FX tiles and explicitly invalidated each icon every time. The FX
render paths now compare visible text/style values before setters and rely on
changed icon-color setters rather than explicit per-revision invalidation.

With `PANELAN_PRESET_TRACE` enabled (`pio run -e
panelan-lvgl-controller-preset-trace`), serial `PRESET_TRACE` milestones include
`t=` (millis). `physical` marks the debounced switch event in the polling loop
(`key`, `kind`); accepted actions have a monotonic `id`, `kind=fx|preset`,
`slot` (zero-based FX) or `target` (one-based hardware preset), and `input_at`.
`pending` marks snapshot publication. `mini_first` marks a completed filled,
wave-marked selected mini card draw with pending intent or number-verified Loading
(if no pending draw completed); `authoritative` marks only the existing
Spark FX_ONOFF/correlated-full or verified hardware-number confirmation, never
an ACK. `mini_final` marks a completed draw of an actually confirmed preset
number or fresh known FX card, never a stale/unknown card. The latter three
include `input_elapsed` in milliseconds; subtract their `t` values for
between-milestone intervals. Existing FX `fx_confirmed elapsed` remains
send-to-confirm, not input-to-confirm. Superseded, rejected or off-screen
requests need not produce mini milestones, and display select/deselect failures
do not produce draw milestones. An action initiated by touch/CLI uses its
acceptance time as `input_at`. Serial timestamps and draw completion are
software observations, not write-only LCD pixel confirmation or live latency
measurements. No SPI or serial logging is performed in BLE/GPIO callbacks.

For FX, the final card must also match that action's requested ON/OFF direction.
If Spark changes to the opposite direction before an off-screen confirmed card
is drawn, that draw does not emit `mini_final` or consume the action ID; a later
matching confirmed redraw can still complete it (unless a new action supersedes
the trace). No protocol confirmation is inferred from mini drawing.

`main_first` and `main_final` are analogous opt-in main-display milestones.
They carry `id`, `kind`, `target`, `input_elapsed` and `t` (the captured millis
at LVGL `REFR_READY`, printed after both the LVGL handler and the mini SPI
update for that loop return). A milestone
requires a changed target card and a completed refresh whose flush area
intersected it; off-screen changes, no-op setters and unrelated redraws do not
count. An armed card survives unrelated snapshot revisions until refresh, but
a superseding action or a changed pending/confirmed stage cancels that arm;
the current action and stage are checked again at refresh completion. A picker
covering home cards cannot produce their milestones. FX final requires fresh
known confirmed ON/OFF matching the action's
requested direction. For presets, `main_first` marks the first visible pending
or number-verified loading main-card update;
`main_final` requires the fresh Ready/full-details main-card update, not just
number confirmation. `number_confirm` can precede `main_first` if no pending
card refresh completed; `full_refresh` precedes the Ready `main_final` refresh.
An open picker covers the main card, so it does not produce preset main
milestones (including for slots 5–8). These are software refresh
completion observations, not proof of physical LCD pixels or Spark authority.

## Preset-mode hardware spot check (2026-10-17)

The default-off early FX verification flag remained disabled. With Spark 2
initially Ready at hardware preset 4, a few serial-CLI preset selections across
slots 1, 2, and 5 were made and then returned to slot 4. All observed changing
requests received matching number confirmation and full-preset Ready; a final
read-only status reported slot 4 Ready, with no pending or failed request. The
test input timestamp is CLI action acceptance, not a physical footswitch edge.

Using linear percentiles (same interpolation as the FX diagnostic harness),
the five correlated action traces used for the table yielded:

| Interval from accepted CLI request | n | p50 | p95 | max |
| --- | ---: | ---: | ---: | ---: |
| Main preset-card first flush | 5 | 67 ms | 67 ms | 67 ms |
| Mini preset-card first draw | 3 | 102 ms | 102 ms | 102 ms |
| Spark hardware-number confirmation | 5 | 512 ms | 520.8 ms | 522 ms |
| Correlated full-preset Ready | 5 | 1,252 ms | 1,269 ms | 1,270 ms |
| Main fully confirmed card update | 2 | 1,236.5 ms | 1,303.6 ms | 1,311 ms |
| Mini fully confirmed card draw | 5 | 655 ms | 663.8 ms | 665 ms |

The sample is small. Raw diagnostic captures for the target-1 and target-2
pending draws contained both first-draw events. The paired stress transcript
contained `main_first` for both actions but no `mini_first`; two other stress
actions (preset 5 and its preset-4 restoration) produced neither first-draw
event. Those missing events are excluded rather than assigned a latency. The
observed main first flushes were prompt; the roughly 1.25 s full-preset Ready
interval is Spark synchronization, not a delay before the main pending card
appears. No physical footswitch input or pixel-level flicker was measured.

Raw diagnostic captures: `/tmp/opencode/panelan_preset_diagnostic_20261017_185620.log`,
`/tmp/opencode/panelan_preset_diagnostic_20261017_190245.log`,
`/tmp/opencode/panelan_preset_diagnostic_20261017_190257.log`. Paired stress
transcript: `/tmp/opencode/panelan_preset_latency_pair_20261017.log`.
