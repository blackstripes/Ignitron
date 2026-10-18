"""Focused trace wiring guards; no firmware or Spark fake required."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1] / 'src'


class MiniActionTraceTest(unittest.TestCase):
    def test_failure_expiry_owned_by_controller_loop(self):
        state = (ROOT / 'controller/ControllerState.cpp').read_text()
        refresh = state.split('void ControllerState::refreshFromSpark(', 1)[1].split(
            'void ControllerState::expectStartupFullPreset(', 1)[0]
        self.assertIn('MiniActionPresentation::failureBadgeExpired(now, presetFailedAtMs_)', refresh)
        self.assertIn('next.failedHardwarePreset = 0;', refresh)
        self.assertIn('MiniActionPresentation::failureBadgeExpired(now, fxFailedAtMs_[i])', refresh)
        self.assertIn('fxFailedAtMs_[i] = now;', refresh)
        self.assertIn('presetFailedAtMs_ = now;', refresh)
        self.assertIn('if (next.pendingHardwarePreset || !next.presetActionFailed) presetFailedAtMs_ = millis();', state)

    def test_disconnect_state_retains_target_and_revokes_local_number_proof(self):
        state = (ROOT / 'controller/ControllerState.cpp').read_text()
        self.assertIn('next.failedHardwarePreset = next.pendingHardwarePreset;', state)
        self.assertIn('next.failedHardwarePreset = MiniActionPresentation::failedTargetAfterFailure(next);', state)
        disconnect = state.split('if (!linkEstablished) {', 1)[1].split('return;', 1)[0]
        self.assertLess(disconnect.index('next.failedHardwarePreset = next.pendingHardwarePreset;'),
                        disconnect.index('next.pendingHardwarePreset = 0;'))
        actions = (ROOT / 'controller/ControllerActions.cpp').read_text()
        self.assertIn('state_.failHardwarePresetRequest();', actions.split('void ControllerActions::onAmpDisconnected()', 1)[1]
                      .split('void ControllerActions::process(', 1)[0])
        self.assertIn('next.locallyVerifiedHardwarePreset = 0;', state)
        self.assertIn('next.locallyVerifiedHardwarePreset = preset;', state)
        self.assertIn('if (numberVerified) next.locallyVerifiedHardwarePreset = preset;', state)

    def test_preset_supersession_and_authority(self):
        actions = (ROOT / 'controller/ControllerActions.cpp').read_text()
        self.assertIn('const uint32_t id = ++presetTraceNextId_;', actions)
        self.assertIn('presetTraceDeferredId_ = id;', actions)
        self.assertIn('presetTraceId_ = presetTraceDeferredId_;', actions)
        self.assertIn('state_.traceMiniAction(id, physicalInputAtMs_', actions)
        self.assertIn('if (state_.snapshot().miniActionId == presetTraceId_ &&', actions)
        self.assertIn('if (matchedQuery && snapshot.confirmedHardwarePreset == sentPreset_)', actions)
        self.assertIn('state_.confirmHardwarePresetRequest(sentPreset_, true);', actions)
        self.assertIn('state_.confirmHardwarePresetRequest(preset);', actions)
        self.assertLess(actions.index('if (matchedQuery && snapshot.confirmedHardwarePreset == sentPreset_)'),
                        actions.index('event=authoritative id=%lu kind=preset'))

    def test_fx_no_duplicate_and_no_ack_confirmation(self):
        actions = (ROOT / 'controller/ControllerActions.cpp').read_text()
        header = (ROOT / 'controller/ControllerState.h').read_text()
        trace_fields = header.split('#ifdef PANELAN_PRESET_TRACE', 1)[1].split('#endif', 1)[0]
        self.assertIn('bool miniActionDesiredEnabled = false;', trace_fields)
        self.assertIn('next.miniActionDesiredEnabled = desiredEnabled;',
                      (ROOT / 'controller/ControllerState.cpp').read_text())
        request = actions.split('bool ControllerActions::requestFxToggle(', 1)[1].split(
            'bool ControllerActions::requestTuner(', 1)[0]
        self.assertLess(request.index('hasPendingFxOperation()'), request.index('queuedFxSlot_ = slot;'))
        self.assertEqual(request.count('++presetTraceNextId_'), 1)
        self.assertIn('state_.beginFxToggleRequest(slot, queuedFxDesiredEnabled_);', request)
        self.assertIn('state_.traceMiniAction(fxTraceId_', request)
        self.assertIn('slot, true, false, queuedFxDesiredEnabled_', request)
        confirm = actions.split('const bool directSuccess =', 1)[1].split('if (presetTargets_.queued() == 0)', 1)[0]
        self.assertIn('modelObservedAfterSend && fx.enabled == sentFxDesiredEnabled_', confirm)
        self.assertIn('matchingFullPreset && fx.known', confirm)
        self.assertIn('const bool desiredEnabled = sentFxDesiredEnabled_;', confirm)
        self.assertIn('state_.traceMiniAction(actionId, inputAt, slot, true, true, desiredEnabled)', confirm)

    def test_draw_milestones_only_after_deselect_and_changed_card(self):
        mini = (ROOT / 'PanelLanMiniDisplaySPI.cpp').read_text()
        draw = mini.split('void PanelLanMiniDisplay::update(', 1)[1]
        self.assertLess(draw.index('needsRedraw('), draw.index('drawCard(*tfts[i], card)'))
        self.assertLess(draw.index('if (!miniSelect.deselect())', draw.index('drawCard(*tfts[i], card)')),
                        draw.index('event=mini_first'))
        self.assertIn('firstDrawId_ != snapshot.miniActionId', draw)
        self.assertIn('finalDrawId_ != snapshot.miniActionId', draw)
        self.assertIn('MiniActionPresentation::presetConfirmed(snapshot, snapshot.miniActionTarget)', draw)
        self.assertIn('MiniActionPresentation::fxConfirmedDirection(snapshot, i, snapshot.miniActionDesiredEnabled)', draw)
        self.assertLess(draw.index('MiniActionPresentation::fxConfirmedDirection('),
                        draw.index('finalDrawId_ = snapshot.miniActionId;'))
        self.assertIn('finalDrawId_ = snapshot.miniActionId;', draw)

    def test_preset_selected_body_precedes_pending_cue(self):
        mini = (ROOT / 'PanelLanMiniDisplaySPI.cpp').read_text()
        preset = mini.split('case PanelLanLVGLUI::View::Preset: {', 1)[1].split('case PanelLanLVGLUI::View::Fx:', 1)[0]
        self.assertIn('MiniActionPresentation::presetSelected(snapshot, target)', preset)
        self.assertIn('card.filled = true; card.mark = Mark::Wave;', preset)
        self.assertIn('card.pending = MiniActionPresentation::mainPresetCard(snapshot).phase', preset)
        self.assertIn('card.filled && card.mark == Mark::Wave', mini)
        self.assertIn('card.pending || MiniActionPresentation::mainPresetCard(snapshot).phase', mini)
        main = (ROOT / 'PanelLanLVGLUI.cpp').read_text()
        status = main.split('void PanelLanLVGLUI::renderStatus(', 1)[1].split(
            'bool PanelLanLVGLUI::mainTraceMatches(', 1)[0]
        self.assertIn('MiniActionPresentation::presetSelected(snapshot, presetCard.target)', status)
        self.assertIn('selectedPreset ? 0x17392F : 0x11181D', status)
        self.assertIn('selectedPreset ? 0x00E65D : 0x34434C', status)
        self.assertIn('pendingPreset ? LV_SYMBOL_BULLET', status)


if __name__ == '__main__':
    unittest.main()
