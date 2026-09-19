# Ignitron Interaction Specification

This document defines how user actions, amp state, visual feedback, touchscreen controls, physical footswitches, mini TFTs, serial commands, and device capabilities interact.

Read with STATE_MODEL.md, AMP_BEHAVIOR.md, UI_DESIGN.md, and PANELAN_PROTOTYPE.md.

When there is a conflict, these state/interaction rules take precedence over concept art.

## General rules

### Same action everywhere

Touchscreen, footswitch, and serial CLI must call the same controller action.

Examples:

- touchscreen Delay tile
- physical Delay footswitch
- CLI fx delay toggle

All three must converge on one controller action path, not duplicate protocol code.

### Spark-owned state is confirmed state

A user action expresses intent. Spark confirmation establishes truth.

UI may show pending immediately, but confirmed state changes only when a
matching Spark-owned state observation proves success. A transport ACK alone
never proves an FX state change.

### One press, one action

Touchscreen:

- act on touch edge/release, not every frame.
- held finger must not repeat unless control is explicitly repeatable.
- preserve current one-command-per-tap behavior.

Physical buttons:

- debounce.
- short press fires only if long press did not fire.
- use current Ignitron long-press constant of 1000 ms initially.
- long press suppresses corresponding short press.

### Do not hide uncertainty

Unknown, stale, unsupported, pending, off, and error are distinct states.

## Global visual-state contract

| State | Tile/button | Mini TFT | Main screen |
| --- | --- | --- | --- |
| Confirmed ON | bright filled accent | bright label + state mark | normal confirmed state |
| Confirmed OFF | dark/subdued | dim label | normal confirmed state |
| Pending | amber outline/pulse | amber pending mark | optional pending badge |
| Unknown | neutral gray + ? | neutral ? | SYNCING/unknown |
| Stale | dim last-known | dim + link-loss mark | RECONNECTING/stale |
| Unsupported | hidden/disabled | blank/not assigned | explain only in setup/diagnostics |
| Error | brief red | brief red if useful | non-blocking error banner |

## Performance view model

Primary MODE cycle:

1. Preset
2. FX
3. Looper if verified supported
4. back to Preset

Tuner is a temporary modal performance state.

For the touchscreen prototype, a newer explicit bottom-navigation tap wins
over a pending tuner entry. If Spark reports a late TUNER_ON after the player
has chosen another destination, remain on that destination and serialize the
native tuner exit; do not let delayed confirmation steal visual focus.

Device and Settings are not part of the physical MODE cycle.

### MODE switch

Normal:

- Preset -> FX
- FX -> Looper if supported
- FX -> Preset if looper unsupported
- Looper -> Preset

If tuner active:

- MODE exits tuner only and returns to previous performance view.
- it does not also advance mode on same press.

If disconnected:

- MODE may change local view for inspection.
- no Spark action is sent.
- Spark controls remain disabled/stale.

### TAP/TUNER switch

Normal:

- short press -> tap tempo
- long press 1000 ms initially -> enter tuner

Tuner active:

- short press -> exit tuner
- long press -> also exit tuner
- no tap-tempo command while tuner active

## Preset screen

Priority:

1. preset name
2. hardware preset/bank number
3. connection state
4. active FX summary
5. device identity secondary

### Preset touch/footswitch

When preset N is selected:

1. require Ready connection.
2. reject/ignore if conflicting preset request pending.
3. create pending preset N.
4. render N as the optimistic green selection and return the prior tile to blue.
5. retain old confirmed preset internally until Spark confirms N.
6. send one native hardware-preset command.
7. when Spark confirms current preset, make N confirmed.
8. clear pending.
9. derive FX chain from new preset.
10. on timeout, clear pending, show error, request current state.

### Already-active preset

Custom UI default: selecting already-active preset does nothing.

Do not preserve original Ignitron hidden behavior where pressing active preset toggles Drive. Dedicated FX mode makes that surprising. If desired later, expose as explicit optional setting.

### Bank browsing

Browsing bank is controller navigation until a preset is actually selected.

Show browsed bank separately from currently sounding preset.

Do not visually imply sound changed when only bank selection changed locally.

## FX screen

Six performance slots:

- Gate
- Comp/Wah
- Drive
- Mod
- Delay
- Reverb

Amp slot omitted from normal toggle UI.

### Coexistence

All six may be on together.

Controller must not invent cross-slot mutual exclusion.

Examples:

- Drive does not force Mod off.
- Delay does not force Reverb off.
- Gate does not force Wah off.

### Comp/Wah special case

Comp/Wah is one slot.

Tile represents current model loaded in that slot:

- compressor model -> COMP or model name
- wah model -> WAH or model name

Toggle means enable/bypass current model.

Initial UI does not switch compressor to wah and does not try to run both simultaneously.

### FX toggle lifecycle

1. if slot unknown/stale, do not blindly invert; resync or show SYNCING.
2. if same slot pending, ignore duplicate.
3. desired state = inverse of confirmed state.
4. mark pending.
5. capture the actual effect model name and current chain identity, then send
   native explicit effect on/off for that model.
6. render amber pending.
7. only a post-send incoming observation of that same model in the desired
   state commits state; final ACK and controller pending/render revisions are
   transport/UI metadata only.
8. a changed preset/chain/model, disconnect, or timeout cancels pending,
   retains the last confirmed visual, and triggers resync when connected.

### External FX changes

If Spark app/amp/other controller changes effect and protocol reports it:

- Spark event wins.
- canonical state updates.
- main tile updates.
- mini TFT updates.
- CLI status updates.
- matching pending resolves.
- conflicting pending fails/cancels.

## Tuner

### Spark 2 native behavior

Spark 2 mutes its own output in tuner mode.

Entering tuner must not:

- turn off individual FX
- bypass amp model
- change preset
- set volume to zero
- stop looper unless hardware testing proves Spark itself does so

Controller sends native tuner ON and relies on amp.

### Enter tuner

1. remember current performance view.
2. set tuner phase Entering.
3. show tuner screen with ENTERING TUNER.
4. send tuner ON.
5. tuner-on event or valid tuner samples establish On.
6. render note/cents.
7. timeout -> error and return previous view.

### While tuner active

- main display presents the tuner as the active performance view.
- bottom navigation remains visible with stable labels. Tapping TUNER exits to
  the prior view; tapping another destination exits first and opens it.
- normal preset/FX/looper actions remain suppressed until tuner exits.
- MODE exits tuner only.
- TAP/TUNER exits tuner.
- six main performance footswitches do not send their normal actions.
- mini TFTs dim or show TUNER/MUTED, not misleading normal labels.

Spark 2's own hardware lets TAP/TUNER or a Preset button exit tuner. Safer controller rule: pressing any primary performance stomp while tuner active exits tuner only; it does not also change tone on same press.

### Exit tuner

1. phase Exiting.
2. send tuner OFF.
3. wait for tuner-off confirmation or use the tested preset-mode exit path on
   Spark 2, which does not reliably publish a distinct tuner-off event.
4. return previous performance view.
5. do not send FX restore sequence.
6. resync current preset/effects if uncertain.
7. Spark native mute ends automatically; controller does not separately unmute.

## Tap tempo

Spark 2 documents TAP tempo as an amp function affecting delay/looper timing.

Rules:

- one shared amp tempo concept initially.
- no separate local Delay BPM and Looper BPM.
- short TAP/TUNER feeds existing tap-tempo logic.
- immediate visual pulse is okay.
- amp-reported BPM authoritative when available.
- tuner active: TAP/TUNER exits tuner, does not count as tap.
- disconnected: no amp command.

## Looper screen — Spark 2 only when verified

The current PanelLan implementation is enabled only for the measured Spark 2
model/serial evidence recorded in `AMP_BEHAVIOR.md`. It requests Spark config
and status before enabling controls. REC/DUB, PLAY/STOP, and UNDO/REDO remain
pending until an incoming looper notification confirms a transport change; a
transport ACK is not confirmation. CLEAR requires one tap to arm and a second
tap within three seconds to send DELETE.

### Record and playback controls

The circular touchscreen controls use concise, state-specific labels; the
heading and guidance line explain the action in plain English:

| Confirmed state | Record control | Playback controls |
| --- | --- | --- |
| Empty | REC | PLAY and STOP (disabled) |
| Recording | FINISH | PLAY and STOP (enabled if status confirms a loop) |
| Stopped with a loop | DUB | Separate PLAY and STOP |
| Playing | DUB | Separate PLAY and STOP |
| Overdubbing | FINISH | Separate PLAY and STOP |
| Unknown transport, fresh nonzero loop count | REC/DUB | Separate PLAY and STOP |

The large heading shows the Spark-confirmed transport, with one short line of
guidance beneath it. Pending requests retain the confirmed heading, add an
amber waiting message, and disable controls until confirmation. Unknown,
stale and disconnected states have explicit messages and disabled controls.
Fresh loop-count status enables Play, Stop, Undo/Redo and Clear even when
transport is unknown. PLAY always requests native playback; the separate STOP
always requests native stop. Neither depends on guessing current transport.
Pending commands still temporarily disable all controls. REC starts recording;
once Spark confirms recording, the same control explicitly reads FINISH and
ends recording with native finish/play. No tap locally asserts success.

### UNDO/REDO

If protocol cannot reliably tell which is next, show combined UNDO/REDO rather than pretending.

### CLEAR

Destructive touchscreen flow:

1. first tap arms clear.
2. tile turns red and says AGAIN; heading asks Clear this loop? and
   explains that the second tap erases the loop.
3. second tap within short window sends delete-all.
4. timeout, navigation, or another looper action cancels.

Physical clear:

- long press only.
- long press suppresses short action.

### Looper + preset/FX

Do not assume preset/FX changes should stop loop.

Let amp native behavior decide.

If Spark continues loop, controller reflects continuing state.

If Spark stops loop, controller reflects received state.

Do not send extra stop command unless user explicitly requested stop.

### Looper + tuner

Hardware test required.

Until verified:

- enter tuner sends only tuner ON.
- do not proactively stop/pause/clear looper.
- hide looper controls while tuner active.
- refresh looper status after exit if possible.

## Device selection

### Boot remembered target

1. load remembered BLE address/model/serial.
2. prefer that address.
3. auto-connect only that target.
4. if unavailable, remain reconnecting/device-needed.
5. do not silently attach to another Spark.

### Scan

Explicit user action:

- show scan progress.
- list candidates.
- selecting different target requires confirmation.
- do not replace remembered device until new target connects and identifies successfully.

### Switch device

1. invalidate all Spark-owned state.
2. clear pending.
3. disconnect old.
4. connect selected.
5. identify.
6. build capabilities.
7. sync current preset/state.
8. persist new target only after success.

## Touch lock

Locked:

- display continues updating.
- physical switches work.
- touchscreen performance controls do nothing.
- visible lock indicator.
- deliberate gesture/settings action required to unlock.
- destructive actions cannot fire accidentally.

## Mini TFT behavior

Mini displays are derived from canonical state + current performance mode.

### Preset mode

Six screens:

- P1 / abbreviated preset
- P2
- P3
- P4
- BANK-
- BANK+

Confirmed active preset bright.
Pending preset amber.
Browsed/non-sounding selection visually distinct.

### FX mode

- GATE
- COMP or WAH based on current model
- DRIVE
- MOD
- DELAY
- REVERB

ON bright.
OFF dim.
Pending amber.
Unknown/stale question/stale treatment.

### Looper mode

State-driven labels where possible:

- REC or DUB
- PLAY or STOP
- UNDO/REDO
- CLEAR
- STATUS/BARS
- CONFIG

Unavailable action rendered disabled.

### Tuner

Normal labels must not imply they are active.

Recommended first behavior: dim all minis and show TUNER on center displays or simple tuner/mute state.

## Serial CLI interoperability

CLI is a peer input surface.

Commands:

- use same ControllerActions path.
- get same pending/confirmed lifecycle.
- respect capability checks.
- status prints confirmed + pending where relevant.

Developer-only unsafe protocol commands, if added later, should be explicit and separate.

## Failure behavior

### Command timeout

- preserve confirmed state.
- clear pending.
- show brief error.
- resync target state.
- do not retry forever.

### BLE disconnect during pending

- fail pending.
- mark Spark-owned state stale.
- reconnect same target.
- resync after reconnect.

### Amp reboot

- detect disconnect.
- Reconnecting.
- reconnect same target.
- re-identify and resync.
- never trust pre-reboot state after reconnect.

### Controller reboot

- reconnect remembered target.
- identify.
- query current state.
- cached state is not authoritative.

## Behavior matrix

| Situation | Controller action | Must NOT happen |
| --- | --- | --- |
| Enter Spark 2 tuner | native tuner ON | do not disable FX one by one |
| Exit tuner | native tuner OFF, return previous view | no FX restore sequence |
| Preset change | one preset command then sync | no manual chain rebuild |
| FX toggle | toggle current effect in slot | do not alter other slots |
| Comp/Wah toggle | bypass current model | do not run Comp + Wah simultaneously |
| Tap tempo | shared amp tempo | no separate local delay/looper BPM |
| BLE drops | stale + reconnect same target | do not show OFF or connect random Spark |
| Different Spark selected | clear amp-owned state + sync | do not carry previous device state |
| Tuner + primary stomp | exit tuner only | do not also change tone |
| Clear loop | confirm/long press | no casual single-tap deletion |

## Hardware verification scenarios

Before locking behavior, test on Spark 2 and record in AMP_BEHAVIOR.md:

1. FX bypass states before/after tuner.
2. looper playback behavior during tuner.
3. preset change while loop plays.
4. FX change while loop plays.
5. tap tempo relationship between delay and looper.
6. notifications when tuner entered from amp physical button.
7. notifications when tuner exits via amp preset button.
8. Spark app changes while controller connected.
9. rapid same-FX command collision.
10. valid looper commands by transport state.

Do not encode guessed behavior where a quick hardware test can establish native amp behavior.
