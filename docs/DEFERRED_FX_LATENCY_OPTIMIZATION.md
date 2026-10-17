# Deferred Spark 2 FX confirmation latency optimization

**Status: resumed for assessment.** All six mini displays have been visually
confirmed working. Immediate mini-card intent feedback and opt-in action timing
milestones are implemented; the Spark confirmation path, firmware deadline,
diagnostic transport behavior, BLE settings, and retry cadence remain unchanged.
ACK-triggered early verification is now a separate compile-time experiment,
**disabled in every PlatformIO environment** pending focused live validation.

## Established baseline

- Spark 2 preset reliability acceptance: **500/500 accepted changing actions / 500 matching Ready actions**. Evidence and metrics: `docs/SPARK_RECEIVE_FRAMING.md`.
- Spark 2 FX reliability acceptance: **500/500 dispatched / 500 controller-confirmed / 500 post-action Ready**. All 500 used correlated full-preset verification. Evidence, raw capture, complete metrics, and the read-only starting baseline: `docs/FX_DIRECT_CONFIRMATION_INVESTIGATION.md`.
- In the 500-action raw capture, all 500 FX mutations received a matching incoming CMD `04` / SUB_CMD `15` generic ACK; zero CMD `03` / SUB_CMD `15` rich FX-state frames appeared. No `03/15` frame discard or incoming rejection was recorded.
- Independent live Spark 2 check: **12/12** ordinary FX mutations received same-message `04/15`; **0/12** received `03/15`.
- `04/15` command-to-ACK latency in the 500 capture: p50 about **6 ms**, p95 **29 ms**, p99 **104 ms**, max **252 ms**. The trace uses host millisecond timestamps.
- Typical correlated full-preset verification latency is about **620–700 ms** (500-action p50 652 ms, p95 662 ms; p99 728 ms, max 831 ms).
- Current controller confirmation latency is about **1.74 s median** (500-action controller elapsed p50 1739 ms).

The ~1.74-second value is **controller confirmation latency, not audio latency**.
Audio switch timing (`BLE FX dispatch → audible DSP/effect state change`) has not
been measured. Do not describe controller confirmation timing as time-to-audible
change.

## Current confirmation path

On the tested Spark 2/NimBLE path, the observed sequence is:

```text
FX command
  → fast matching generic 04/15 ACK
  → existing initial grace period for a possible fresh 03/15 observation
  → no 03/15 observed in the preserved 500-action capture or 12-action live check
  → correlated full-preset verification
  → authoritative controller confirmation from Spark-owned matching state
```

The generic ACK is **not authoritative**. It remains only a transport milestone;
the controller confirms from a fresh matching `FX_ONOFF` observation or a
correlated full-preset response. The full verification takes roughly 0.65 s after
dispatch, on top of the initial post-send grace period. Preserve these Spark-owned
checks and the 15-second firmware confirmation deadline in any future work.

The 150- and 500-action FX acceptance captures and the cross-project
`proven`/`plausible`/`speculative` source comparison are preserved in
`docs/FX_DIRECT_CONFIRMATION_INVESTIGATION.md`. The current controller-owned
`fx_confirmed` event is emitted after state/counter/persistent bookkeeping and
pending-request cleanup; its firmware `elapsed` is independently checked by the
host diagnostic.

## Future investigation opportunities

### Current mini-feedback measurement and early-verification decision (2026-10-17)

- The previous firmware had no correlated input-to-mini-draw milestones, so a
  pre-change visual-latency distribution is unavailable. New `PANELAN_PRESET_TRACE`
  milestones are documented in `docs/MINI_ACTION_FEEDBACK.md`.
- A 12-action live Spark 2 inverse-pair diagnostic completed **12/12 confirmed**,
  with 12 correlated full-preset verifications, no retries, deferrals, or
  transport/parser anomalies. Actions ran through the CLI while the mini
  renderer remained in Preset view, so there were correctly no FX-card
  `mini_first`/`mini_final` events; this run does **not** measure mini visual
  latency. Full-confirmed state was restored for each FX slot by its pair.
- In this sample, input-to-authoritative confirmation from the new trace had
  p50 **1,918.5 ms**, nearest-rank p95 **2,097 ms**, max **2,097 ms**. The
  diagnostic's host command-send-to-confirm metric was p50 **1,754 ms**, p95
  **1,861 ms**, max **1,943 ms**. These are controller confirmation timings,
  not audible FX latency. The small sample and CLI input path are not a physical
  switch or 150-action acceptance run. Raw serial: `/tmp/opencode/panelan_fx_20261017_122956_813945.log`;
  summary output: `/home/pzwolinski/.local/share/opencode/tool-output/tool_14aea5181001Wz2iaVfBZ7Fi7r`.
- The early-verification proposal remains technically promising: the historical
  500-action baseline had about a one-second ACK grace before a roughly 652 ms
  correlated full query, while current controller confirmation p50 was 1,739 ms.
  The live run did not exercise early-query behavior. The opt-in helper exists,
  but must remain disabled until a safe operator window for the required roughly
  150-action live run. A generic ACK remains non-authoritative.
- Physical visual confirmation of the new preset-position order and live
  `mini_first`/`mini_final` measurements remain outstanding. The serial monitor's
  `LiveDone` events do not prove visible pixels.

### A. Measure actual audio latency

Measure:

```text
BLE FX dispatch → audible DSP/effect state change
```

Prefer synchronized GPIO/audio capture if quantitative timing is needed. A quick
subjective stomp/listen check may precede it, but it is not a numeric latency
measurement.

### B. Evaluate safe confirmation-speed optimization

Implemented as a **default-off candidate**, not enabled or tested on hardware:

```text
FX command
  → matching correlated 04/15 ACK
  → immediately start authoritative full-preset verification
  → confirm only from Spark-owned matching state
```

Only an explicit `-DPANELAN_EARLY_FX_VERIFY=1` at compile time enables the
candidate; no checked-in PlatformIO environment sets it. The controller drains
the active command's matching `04/15` ACK and skips the first 1000 ms grace
only when the observed amp name is exactly `Spark 2`. A missing/mismatched ACK,
NEO Core, and all default builds retain the 1000 ms grace. Transport Busy may
defer the query; it is not forced through the scheduler. Later attempts still
use the 5000 ms response window and the original command's 15000 ms deadline.
The ACK triggers verification sooner but remains non-authoritative. The
measured components make a confirmed-state latency around **0.65–0.75 s**
plausible; it is not a measured result. Do not change the firmware deadline,
retry cadence, response-lane behavior, or Spark correlation rules.

### C. Desktop/Bleak versus ESP32/NimBLE A/B

`tools/probe_spark_fx_confirmation.py` is prepared for a later same-amp comparison.
The unresolved question is:

> Does the same Spark 2 send `03/15` to a desktop/Bleak central while sending only
> `04/15` to an ESP32/NimBLE central?

Do not claim an answer until that A/B is performed. The current Ignitron/Spark
connection must not be conflicted; the desktop mutation path was not run in this
investigation.

### D. Possible BLE-variable experiments

Only if the desktop A/B justifies them, test **one variable at a time**:

- negotiated MTU;
- connection parameters;
- notification subscription behavior;
- write type;
- timing;
- BLE central stack.

Do not experiment with these unless the controlled desktop/NimBLE A/B justifies
the variable and the change is independently testable.

## Resume checklist

1. Confirm the Spark 2 preset 500/500 and FX 500/500 acceptance evidence still
   represents the baseline; start with read-only authoritative status.
2. Measure subjective or quantitative audio-switch latency separately from
   controller confirmation latency.
3. Run the desktop/Bleak versus ESP32/NimBLE `03/15` vs `04/15` A/B if still
   useful and safe on an isolated connection.
4. [Done, default-off] Implement ACK-triggered early full-preset verification
   behind a narrowly scoped compile-time change; the ACK must not confirm state.
5. [Done for host models] Run focused host regressions for correlation, deadlines,
   stale/conflicting observations, and controller proof in both default and
   candidate modes. This does not substitute for an on-device validation.
6. Run about 150 FX actions and compare confirmation latency and reliability.
7. Only then decide whether another 500-action acceptance is warranted.

Items not completed in this task remain open in the checklist above. In
particular, visual mini-card latency still needs an FX-view run, physical layout
confirmation is still requested, and early verification still needs a safe
150-action live run before it could be enabled in shipping firmware.
