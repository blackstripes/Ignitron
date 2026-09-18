# Spark Amp Native Behavior and Verification Matrix

The custom Ignitron controller should rely on the Spark device for behavior the amp already owns instead of recreating that logic locally.

This document separates verified product behavior, verified protocol behavior, assumptions that must not become code, and hardware tests still required.

## Design rule: let the amp do amp things

Before adding controller logic, ask:

Does Spark already perform this transition atomically and correctly?

If yes, send the smallest native Spark command and observe the resulting state.

Examples:

- enter tuner with native tuner command.
- change preset with native preset command.
- bypass an FX slot with native effect command.
- drive looper with native looper commands.
- use native tempo behavior.

Avoid multi-command sequences that imitate behavior the amp already owns.

Benefits:

- fewer race conditions
- less desynchronization
- behavior matches Spark's own buttons/app
- firmware updates are more likely to remain compatible
- controller observes state instead of manufacturing it

## Spark 2 tuner: verified native mute

Positive Grid's official Spark 2 tuner documentation states that Spark 2 is muted when entering tuner mode.

Implications:

- Ignitron sends tuner ON.
- Spark 2 handles muting.
- Ignitron must not turn off Gate/Comp/Drive/Mod/Delay/Reverb as a mute strategy.
- Ignitron must not set Master/Guitar volume to zero.
- Ignitron must not remember and replay FX states just to restore from tuner.
- exit with tuner OFF and let Spark restore normal output.

Important: muted output is not the same thing as all effects being bypassed.

Official documentation verifies mute, but does not say Spark rewrites every effect bypass state. Therefore controller must not assume FX states become off.

Source:
https://help.positivegrid.com/hc/en-us/articles/28137133610381-Onboard-Tuner-on-Spark-2

That page also documents exit by TAP/TUNER or Preset button on the amp itself.

## Tuner protocol already exists in Ignitron

Ignitron native tuner on/off command:

- command 0x01
- subcommand 0x65
- payload on/off

SparkStreamReader already parses:

- tuner note + pitch offset
- tuner ON
- tuner OFF

Therefore the custom UI can model requested vs confirmed tuner state instead of treating tuner screen visibility as proof.

## Spark 2 external tuner observations

Hardware testing found that Spark 2 emits native tuner ON/OFF and tuner-output
messages when tuner is entered externally. These messages are observations of
amp-owned state, not requests from Ignitron. The receive path must therefore
only update its local submode (and synchronize the pending preset view on an
ON/OFF transition); it must never call the local `switchSubMode` or
`switchTuner` command path in response. In particular, a tuner-output sample
means tuner is active and must never cause Ignitron to send tuner OFF.

The controller records each successfully parsed note/offset sample with a
monotonic receive revision and timestamp. The read-only PanelLan tuner surface
shows it only while the external tuner remains active and the sample is fresh;
otherwise it reports that it is listening rather than retaining a misleading
old note. `OFFSET` is intentionally used as the label until its units are
calibrated against hardware.

The same test exposed a legacy crash: an externally received tuner ON invoked
`switchSubMode`, which stopped the optional BLE keyboard even when no keyboard
BLE server had been initialized. `SparkBLEKeyboard::start/end` now safely no-op
when `BLEDevice::getServer()` is null. Explicit local tuner commands retain
their existing behavior.

## Spark 2 firmware note around tuner exit

Positive Grid firmware 2.5.2.149 / rev479 notes a fix for missing preset status when exiting tuner.

Implications:

- preset/status synchronization around tuner is native firmware behavior.
- do not invent preset state after tuner.
- if current state is not clearly refreshed, query/resync.

Source:
https://help.positivegrid.com/hc/en-us/articles/28160429394317-Spark-2-Firmware-Release-Notes

## Tap tempo is amp-native

Positive Grid documents Spark 2 TAP as setting tempo used by delay effects and looper/signal-chain timing.

Implications:

- start with one shared tempo concept.
- do not create separate local Delay BPM and Looper BPM unless tests prove separate values.
- use native tap behavior instead of directly editing many parameters.

Source:
https://help.positivegrid.com/hc/en-us/articles/29746925029645-Tap-Tempo-on-Spark-2

## Effects use slots

Positive Grid describes Spark 2 with up to three pre-amp effects, three post-amp effects, and one amp model per preset.

Ignitron represents seven ordered slots:

1. Noise Gate
2. Compressor/Wah
3. Drive
4. Amp
5. Modulation/EQ
6. Delay
7. Reverb

Cross-slot behavior:

- controller permits independent slots to coexist.
- do not invent rules such as Drive off when Mod on.
- if Spark accepts those slots enabled, controller reflects it.

Same-slot behavior:

- a slot contains one current effect model.
- Comp/Wah is one slot, not two simultaneous independent controls.
- first UI toggles current model's enabled/bypassed state.
- model selection is a future tone-editing feature.

Spark 2 source:
https://help.positivegrid.com/hc/en-us/articles/28134103894157-Technical-Specification-for-Spark-2

NEO Core also groups Compressor/Wah:
https://help.positivegrid.com/hc/en-us/articles/37422840800781-Spark-NEO-Core-Technical-Specification

## FX toggle protocol exists

Ignitron effect on/off command:

- command 0x01
- subcommand 0x15
- effect model name
- desired on/off

Incoming FX state updates active preset. The effect-model name and reported
on/off value are the confirmation signal for controller FX actions.

Legacy code has pending/active behavior that can promote pending data after a
final ACK. The controller action boundary must not inherit that ACK-as-success
semantics for FX controls.

Implication:

- preserve pending/confirmed lifecycle.
- do not permanently flip tile simply because user touched it.
- do not use a final transport ACK as confirmation; it only proves the command
  reached the protocol acknowledgement path.
- confirm only after either a post-send FX_ONOFF observation for the same
  requested effect model, or a post-send full-preset response that reports
  that exact model in the requested state. Both observations must be applied
  to SparkPresetControl before ControllerActions consumes them.
- NEO Core may return the final `0x15` transport ACK without an FX_ONOFF
  notification. For the matching serialized action, use that ACK only to
  request the full current preset; the ACK itself is never confirmation.
- the protocol does not correlate that observation to a specific outgoing
  command. A matching external Spark/App event after the request is therefore
  indistinguishable from the controller's result and resolves to the same
  Spark-owned state; a conflicting event cancels/fails the pending intent.

## Preset changes should remain atomic

Preset contains effect models, bypass states, amp model, parameters, and BPM-related data.

Controller behavior:

1. request native preset change.
2. show pending preset.
3. wait for Spark confirmation/current preset.
4. replace derived FX/preset state.

Do not:

- toggle each effect manually to approximate target preset.
- carry old slot states into new preset.
- assume same effect names/settings survive.

## Looper should use native transport

Spark 2 has onboard looper and Ignitron already understands native looper commands/status.

Implications:

- request native operations.
- display amp-reported state where available.
- do not say PLAYING solely because Play was pressed.

Spark 2 documents up to 60 seconds onboard looper recording.

Source:
https://help.positivegrid.com/hc/en-us/articles/28134103894157-Technical-Specification-for-Spark-2

## Known / unknown matrix

| Behavior | Spark 2 | NEO Core | Controller rule |
| --- | --- | --- | --- |
| Direct BLE preset switching | needs final Spark 2 validation in this fork | Verified on hardware | native preset command |
| Tuner protocol | Verified on hardware: local/external entry, exit path, and pitch samples | native ON probe produced no ON/OFF event or pitch sample in 5 s | capability-gate by evidence |
| Tuner mutes output | Verified by Positive Grid and hardware | unknown | rely on amp; do not simulate |
| Tuner turns FX bypass states off | Not documented | unknown | assume no; never rewrite FX for tuner |
| Tap tempo amp-native | Verified by Positive Grid | device-specific validation needed | native tap logic |
| Internal looper | Supported | no onboard looper documented | show only on verified supported device |
| Loop continues across preset change | unknown | N/A/unknown | hardware test; do not force stop |
| Loop behavior during tuner | unknown | N/A/unknown | hardware test; do not force stop |
| FX change while loop plays | unknown signal-path impact | N/A/unknown | hardware test |
| External app FX changes reflected | protocol supports incoming FX state | likely but not fully tested | Spark event wins |
| Comp + Wah simultaneously in same slot | no, by slot model | same category grouping | one current model |

## Hardware verification matrix

Record actual results here as tests are performed.

### T1 — Spark 2 tuner preserves FX states

Setup:

1. choose preset with recognizable mix of ON/OFF FX.
2. capture controller-reported state.
3. enter tuner from Spark 2 physical TAP/TUNER.
4. observe BLE messages.
5. exit tuner.
6. inspect current preset/FX.

Questions:

- are bypass states unchanged?
- does Spark send tuner ON/OFF?
- does it resend preset status on exit?
- do any effect on/off messages occur solely because tuner entered?

Expected controller behavior unless disproven:

- preserve FX state.
- rely on native mute.

### T2 — Controller-entered tuner

1. enter tuner through Ignitron.
2. verify Spark output mutes.
3. verify pitch samples.
4. exit through Ignitron.
5. verify output returns.
6. verify preset/FX state.

Goal: prove controller tuner matches native amp tuner.

**2026-09-18 result:** PanelLan display entry/exit, native mute, live note,
and cents rendering were exercised on Spark 2. Spark 2 can leave a pitch packet
queued after an explicit exit; the controller ignores that short tail so it
does not reopen tuner. Continue recording preset/FX-resync observations here.

### T3 — Looper playback + tuner

1. record a loop.
2. play loop.
3. enter tuner.
4. determine whether loop continues audible, continues muted, pauses, or stops.
5. exit tuner.
6. inspect looper state.

Do not add stop command; reflect native outcome.

### T4 — Looper playback + preset change

1. record/play loop.
2. change hardware preset.
3. observe whether loop continues.
4. observe sound changes.
5. record BLE messages.

Do not force stop.

### T5 — Looper playback + FX toggle

With loop playing:

1. toggle Drive.
2. toggle Delay.
3. determine impact on live guitar, recorded loop, or both.
4. observe protocol.

Controller should report outcome, not add extra commands.

### T6 — Enter tuner from amp physical control

Verify whether controller receives:

- tuner ON
- tuner note samples
- preset/FX changes

Controller should enter tuner UI if amp says tuner active, even if controller did not initiate it.

### T7 — Amp preset button exits tuner

With tuner active, press Spark 2 preset button.

Observe:

- tuner OFF
- preset selection
- event ordering

This defines external transition handling.

### T8 — Spark app interoperability

With Ignitron connected:

- change preset in Spark app
- toggle each FX category
- enter/exit tuner if possible
- change tempo

Controller should converge to Spark state without local input.

### T9 — Command collision

Rapidly request two toggles on same FX slot.

Desired controller behavior:

- second action ignored while pending.
- no oscillation/command storm.
- final UI agrees with amp.

### T10 — NEO Core tuner

1. issue tuner ON through Ignitron.
2. confirm tuner data.
3. determine whether guitar output mutes.
4. exit.

**2026-09-18 probe:** `tuner probe on` sent the native command while NEO Core
was connected. No tuner ON/OFF notification or pitch sample arrived during the
five-second capture window. Native OFF was then sent. Audio mute could not be
observed through serial and remains unverified. Do not enable tuner controls
on NEO Core without a repeatable positive protocol trace.
5. inspect preset/FX state.

Only after this mark tuner Verified for NEO Core.

## Adding new controller behavior

Before adding local automation around an amp feature:

1. check Positive Grid documentation.
2. inspect existing Ignitron protocol path.
3. test real amp if interaction ambiguous.
4. record result here.
5. prefer one native amp command over a multi-command imitation.

The goal is not to outsmart Spark firmware. The goal is to expose its behavior cleanly, reliably, and visibly.
