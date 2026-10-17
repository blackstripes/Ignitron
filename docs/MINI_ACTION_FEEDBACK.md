# Mini action feedback and timing

The SPI mini cards derive their appearance from `ControllerSnapshot` in the main
loop, after the LVGL update. A pending FX card displays the requested ON/OFF
direction (without changing Spark's confirmed `enabled` value) and a small
amber corner dot. Confirmation removes the dot without changing the ON/OFF
appearance. Failure, timeout, link loss and stale state continue to use Spark
observations; preset failures mark only the affected slot. Pending preset
selection still takes precedence over the old confirmed number. Unchanged
card keys are not sent to SPI; the pending card is serviced first.
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

With `PANELAN_PRESET_TRACE` enabled (`pio run -e
panelan-lvgl-controller-preset-trace`), serial `PRESET_TRACE` milestones include
`t=` (millis). `physical` marks the debounced switch event in the polling loop
(`key`, `kind`); accepted actions have a monotonic `id`, `kind=fx|preset`,
`slot` (zero-based FX) or `target` (one-based hardware preset), and `input_at`.
`pending` marks snapshot publication. `mini_first` marks a completed selected
mini card draw with pending intent; `authoritative` marks only the existing
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
