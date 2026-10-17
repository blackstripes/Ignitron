"""Integration guards for the main-loop-only MCP input/controller boundary."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / 'src'


class PhysicalSwitchSourceTest(unittest.TestCase):
    def test_loop_and_card_targets(self):
        app = (ROOT / 'Ignitron.ino').read_text()
        mini = (ROOT / 'PanelLanMiniDisplaySPI.cpp').read_text()
        mini_header = (ROOT / 'PanelLanMiniDisplay.h').read_text()
        self.assertIn('bool readSwitches(uint8_t &pressed);', mini_header)
        self.assertIn('physicalSwitches.sample(valid, pressed, now', app)
        self.assertIn('physicalRouting.event(key, kind, count, controllerState.snapshot(), panelLanDisplay, controllerActions)', app)
        self.assertEqual(app.count('pollPhysicalSwitches();'), 1)
        self.assertLess(app.index('pollPhysicalSwitches();'), app.index('if (!sparkConnected)'))
        self.assertIn('now - lastPollAt < 5', app)
        self.assertIn('physicalRouting.observe(controllerState.snapshot(), count)', app)
        routing = (ROOT / 'controller/PhysicalSwitchRouting.h').read_text()
        layout = (ROOT / 'controller/PhysicalPresetLayout.h').read_text()
        self.assertIn('PhysicalPresetLayout::positions[i]', mini)
        self.assertIn('PhysicalPresetLayout::positions[key]', routing)
        self.assertIn('bank_.target(position.slot - 1, count)', routing)
        self.assertIn('browsedBank * 4 + position.slot', mini)
        self.assertIn('"P%u %s", position.slot', mini)
        for entry in ('{Role::Slot, 4}', '{Role::BankPrevious, 0}', '{Role::BankNext, 0}',
                      '{Role::Slot, 1}', '{Role::Slot, 2}', '{Role::Slot, 3}'):
            self.assertIn(entry, layout)
        self.assertIn('snapshot.pendingHardwarePreset == target', mini)
        self.assertIn('snapshot.confirmedHardwarePreset == target', mini)
        self.assertIn('previous ? browsedBank > 0 : (browsedBank + 1) * 4 < presetCount', mini)

    def test_shared_tap_path(self):
        app = (ROOT / 'controller/PhysicalSwitchRouting.h').read_text()
        cli = (ROOT / 'SparkSerialCLI.cpp').read_text()
        actions = (ROOT / 'controller/ControllerActions.cpp').read_text()
        self.assertIn('actions.requestTapTempo()', app)
        self.assertIn('controllerActions_->requestTapTempo()', cli)
        self.assertIn('dataControl.tapTempoButton()', actions)
        self.assertNotIn('SparkDataControl', app)

    def test_tap_and_other_actions_are_mutually_exclusive(self):
        actions = (ROOT / 'controller/ControllerActions.cpp').read_text()

        def request(name, next_signature):
            return actions.split(f'bool ControllerActions::{name}(', 1)[1].split(
                next_signature, 1)[0]

        preset = request('requestHardwarePreset', 'bool ControllerActions::requestFxToggle(')
        fx = request('requestFxToggle', 'bool ControllerActions::requestTuner(')
        tuner = request('requestTuner', 'bool ControllerActions::requestTapTempo(')
        tap = request('requestTapTempo', 'void ControllerActions::cancelTunerEntry(')
        looper = request('canRequestLooper', 'bool ControllerActions::queueLooperAction(')

        for gate in (preset, fx, tuner, looper):
            self.assertIn('queuedTaps_', gate)
        for pending in ('s.pendingHardwarePreset', 'sentPreset_', 'presetTargets_.queued()',
                        'hasPendingFxOperation()', 'queuedTunerRequest_', 'tunerRequestSent_',
                        's.looperPending', 'queuedLooperAction_', 'sentLooperAction_'):
            self.assertIn(pending, tap)
        self.assertIn('queuedTaps_ == 8', tap)

    def test_tuner_entry_precedence(self):
        routing = (ROOT / 'controller/PhysicalSwitchRouting.h').read_text()
        ui = (ROOT / 'PanelLanLVGLUI.cpp').read_text()
        self.assertIn('s.tunerActive || view == decltype(view)::Tuner', routing)
        self.assertLess(routing.index('ui.physicalTunerPress(s.tunerActive)'), routing.index('if (key == 6)'))
        self.assertIn('if (activeScreen_ != Screen::Tuner) screenBeforeTuner_ = activeScreen_;', ui)
        navigation = ui.split('void PanelLanLVGLUI::physicalNavigate(', 1)[1].split(
            'void PanelLanLVGLUI::physicalTunerPress(', 1)[0]
        self.assertLess(navigation.index('actions_->requestTuner(true)'), navigation.index('setActiveScreen(Screen::Tuner)'))
        self.assertIn('actions_->cancelTunerEntry()', ui)
        self.assertIn('setActiveScreen(screenBeforeTuner_)', ui)
        self.assertIn('actions_->requestTuner(false)', ui)
        self.assertNotIn('activeScreen_ == Screen::Tuner ? Screen::Preset', ui)


if __name__ == '__main__':
    unittest.main()
