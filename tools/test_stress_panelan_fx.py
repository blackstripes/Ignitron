import unittest
from unittest.mock import patch

from stress_panelan_fx import (SLOTS, Step, Unresolved, run, run_confirmed_step,
                               run_step, status_is_ready)


def send(slot, model="Model", direction="on"):
    return f"Controller: sending FX {slot} ({model}) {direction}"


def confirm(slot, model="Model", source="FX_ONOFF"):
    return f"Controller: FX {slot} ({model}) confirmed by {source}"


def observed(model="Model", direction="on", source="FX_ONOFF"):
    state = "true" if direction == "on" else "false"
    if source == "FX_ONOFF":
        return ["Message processed:", f'{{"Effect": "{model}", "IsOn": {state}}}']
    return ["Message processed:", '{"Name": "Preset", "Pedals": [',
            f'{{"Name": "{model}", "IsOn": {state}}}', ']}']


class ScriptedIO:
    def __init__(self, lines):
        self.lines = iter(lines)
        self.commands = []

    def write(self, command):
        self.commands.append(command)

    def read(self, deadline):
        return next(self.lines, None)


class FxTest(unittest.TestCase):
    def test_ready(self):
        ready = ["Spark connected: yes", "Amp: Spark", "Serial: 123", "Preset name: CLEAN"]
        self.assertTrue(status_is_ready(ready))
        for i, replacement in enumerate(("Spark connected: no", "Amp: (unknown)",
                                          "Serial: (unknown)", "Preset name: (unknown)")):
            lines = ready.copy()
            lines[i] = replacement
            self.assertFalse(status_is_ready(lines))

    def test_ready_requires_confirmed_name_not_startup_unknown(self):
        self.assertFalse(status_is_ready([
            "Spark connected: yes", "Amp: Spark NEO Core", "Serial: SHP1",
            "Preset name: (unknown)" ]))

    def test_six_names_indices_and_sources(self):
        for slot, name in enumerate(SLOTS):
            with self.subTest(name=name):
                source = "FX_ONOFF" if slot % 2 else "full preset response"
                io = ScriptedIO(["Effect toggle requested.", send(slot, name)] +
                                observed(name, source=source) + [confirm(slot, name, source)])
                step = run_step(io, slot, 0.1)
                self.assertEqual(io.commands, [f"fx {name} toggle"])
                self.assertEqual((step.model, step.direction, step.source), (name, "on", source))

    def test_confirmation_must_follow_matching_send_in_same_window(self):
        for lines in ([confirm(0)], [send(0), confirm(1)],
                      [send(0), confirm(0, "other")], [send(0), send(0)],
                      [send(0), "Controller: FX 0 cancelled: confirmation timed out"],
                      [REJECT := "Effect command failed or was rejected."],
                      [send(0), confirm(0), confirm(0)]):
            with self.subTest(lines=lines):
                step = Step(0)
                with self.assertRaises(Unresolved):
                    for line in lines:
                        step.observe(line)

    def test_controller_send_can_be_embedded_after_non_newline_spark_log(self):
        io = ScriptedIO(["Effect toggle requested.",
                         "Switching On effect bias.noisegate...Controller: sending FX 0 (bias.noisegate) on"] +
                        observed("bias.noisegate", source="full preset response") +
                        ["Controller: FX 0 (bias.noisegate) confirmed by full preset response"])
        step = run_step(io, 0, 0.1, probes=False)
        self.assertTrue(step.confirmed)
        self.assertEqual(step.model, "bias.noisegate")

    def test_full_preset_off_observation_confirms_inverse(self):
        first = Step(0)
        first.observe(send(0))
        io = ScriptedIO([send(0, direction="off")] +
                        observed(direction="off", source="full preset response") +
                        [confirm(0, source="full preset response")])
        step = run_step(io, 0, 0.1, inverse_of=first)
        self.assertTrue(step.confirmed)
        self.assertEqual(step.direction, "off")

    def test_busy_duplicate_and_preset_must_be_rejected_before_confirmation(self):
        prefix = [send(0), "Effect command failed or was rejected.",
                  "PRESET_TRACE event=reject target=2 reason=fx phase=4",
                  "Preset request rejected; wait for synchronization to finish."]
        io = ScriptedIO(prefix + observed() + [confirm(0)])
        self.assertTrue(run_step(io, 0, 0.1, True, 2).confirmed)
        self.assertEqual(io.commands, ["fx gate toggle", "fx gate toggle", "preset 2"])
        for lines in ([send(0), confirm(0)],
                      [send(0), "Effect command failed or was rejected.", confirm(0)],
                      [send(0), "PRESET_TRACE event=accept target=2"],
                      [send(0), "Preset 2 requested."],
                      [send(0), "PRESET_TRACE event=reject target=2 reason=phase", confirm(0)]):
            with self.subTest(lines=lines):
                with self.assertRaises(Unresolved):
                    run_step(ScriptedIO(lines), 0, 0.1, True, 2)

    def test_timeout_and_failed_first_never_restore(self):
        for lines in ([send(0)], ["Effect command failed or was rejected."],
                      [send(0), "Controller: FX 0 cancelled: disconnect"]):
            io = ScriptedIO(["--- Ignitron status ---", "Spark connected: yes",
                             "Amp: Spark", "Serial: 123", "Preset name: CLEAN",
                             "Looper loops: 0"] + lines)
            with self.subTest(lines=lines), self.assertRaises(Unresolved):
                run(io, 0.001, 0.1, False, None)
            self.assertEqual(io.commands.count("fx gate toggle"), 1)

    def test_rejection_before_send_waits_for_sync_before_retrying(self):
        ready = ["--- Ignitron status ---", "Spark connected: yes", "Amp: Spark",
                 "Serial: 123", "Preset name: CLEAN", "Looper loops: 0"]
        io = ScriptedIO(["Effect command failed or was rejected."] + ready +
                        ["Effect toggle requested.", send(0)] + observed() + [confirm(0)])
        with patch("stress_panelan_fx.time.sleep"):
            step = run_confirmed_step(io, 0, 0.1, 0.1)
        self.assertTrue(step.confirmed)
        self.assertEqual(io.commands, ["fx gate toggle", "status", "fx gate toggle"])

    def test_repeated_pre_send_rejection_is_bounded_without_fx_send(self):
        ready = ["--- Ignitron status ---", "Spark connected: yes", "Amp: Spark",
                 "Serial: 123", "Preset name: CLEAN", "Looper loops: 0"]
        io = ScriptedIO(["Effect command failed or was rejected."] + ready +
                        ["Effect command failed or was rejected."] + ready +
                        ["Effect command failed or was rejected."])
        with patch("stress_panelan_fx.time.sleep"), self.assertRaises(Unresolved):
            run_confirmed_step(io, 0, 0.1, 0.1)
        self.assertEqual(io.commands.count("fx gate toggle"), 3)

    def test_deadline_and_missing_confirmation(self):
        io = ScriptedIO([send(0)])
        with self.assertRaises(Unresolved):
            run_step(io, 0, 0.001)
        self.assertEqual(io.commands, ["fx gate toggle"])
        with self.assertRaises(Unresolved):
            run_step(ScriptedIO([]), 0, 0.001)

    def test_false_confirmation_wrong_state_and_other_model_fail_closed(self):
        for payload in ([], observed(direction="off"), observed("other"),
                        observed(source="full preset response")):
            io = ScriptedIO([send(0)] + payload + [confirm(0)])
            with self.subTest(payload=payload), self.assertRaises(Unresolved):
                run_step(io, 0, 0.1)
            self.assertEqual(io.commands, ["fx gate toggle"])
        io = ScriptedIO([send(0)] + observed(direction="off", source="full preset response") +
                        [confirm(0, source="full preset response")])
        with self.assertRaises(Unresolved):
            run_step(io, 0, 0.1)
        self.assertEqual(io.commands, ["fx gate toggle"])

    def test_contradictory_spark_observations_fail_even_if_confirmation_source_matches(self):
        io = ScriptedIO([send(0), "Message processed:",
                         '{"Effect": "Model", "IsOn": true}',
                         "Message processed:", '{"Pedals": [{"Name": "Model", "IsOn": false}]}',
                         confirm(0, source="FX_ONOFF")])
        with self.assertRaises(Unresolved):
            run_step(io, 0, 0.1)

    def test_second_direction_or_model_mismatch_stops_before_confirmation(self):
        first = Step(0)
        first.observe(send(0))
        for line in (send(0), send(0, "other", "off")):
            io = ScriptedIO([line, confirm(0)])
            with self.subTest(line=line), self.assertRaises(Unresolved):
                run_step(io, 0, 0.1, inverse_of=first)
            self.assertEqual(io.commands, ["fx gate toggle"])

    def test_bounded_latency_does_not_send_followup_after_timeout(self):
        class ClockIO(ScriptedIO):
            def read(self, deadline):
                clock[0] += 0.51
                return super().read(deadline) if clock[0] < deadline else None

        clock = [0.0]
        with patch("stress_panelan_fx.time.monotonic", side_effect=lambda: clock[0]):
            io = ClockIO([send(0), confirm(0)])
            with self.assertRaises(Unresolved):
                run_step(io, 0, 1.0)
            self.assertEqual(io.commands, ["fx gate toggle"])
            clock[0] = 0
            io = ClockIO([send(0)] + observed() + [confirm(0)])
            self.assertTrue(run_step(io, 0, 2.05).confirmed)

    def test_complete_run_exactly_twelve_toggles(self):
        def ready():
            return ["--- Ignitron status ---", "Spark connected: yes", "Amp: Spark",
                    "Serial: 123", "Preset name: CLEAN", "Looper loops: 0"]

        lines = ready()
        for slot in range(6):
            for direction in ("on", "off"):
                lines += ["Effect toggle requested.", send(slot, SLOTS[slot], direction)]
                if direction == "on":
                    lines += ["Effect command failed or was rejected."]
                lines += observed(SLOTS[slot], direction) + [confirm(slot, SLOTS[slot])]
                if not (slot == 5 and direction == "off"):
                    lines += ready()
        io = ScriptedIO(lines)
        run(io, 0.1, 0.1, True, None)
        self.assertEqual(io.commands.count("fx gate toggle"), 3)  # first, busy probe, inverse
        self.assertEqual(sum(cmd.startswith("fx ") for cmd in io.commands), 18)


if __name__ == "__main__":
    unittest.main()
