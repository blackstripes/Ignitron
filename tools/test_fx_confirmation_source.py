"""Host-only wiring contract for controller-owned FX confirmation proof."""
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class FxConfirmationSourceTest(unittest.TestCase):
    def test_confirmation_helper_commits_bookkeeping_before_diagnostics(self):
        helper = (ROOT / 'src/controller/FxConfirmation.h').read_text()
        ordered = ('confirmState();', 'recordTelemetry();', 'recordPersistent();',
                   'clearRequest();', 'log();')
        positions = [helper.index(token) for token in ordered]
        self.assertEqual(positions, sorted(positions))

    def test_both_authoritative_paths_use_the_post_commit_confirmation_event(self):
        source = (ROOT / 'src/controller/ControllerActions.cpp').read_text()
        fx = source.split('if (sentFxSlot_ != kNoFxSlot) {', 1)[1].split(
            'if (presetTargets_.queued() == 0) {', 1)[0]
        direct_guard = ('const bool directSuccess = modelObservedAfterSend && fx.enabled == sentFxDesiredEnabled_ &&\n'
                        '            fx.enabled != fxEnabledBeforeRequest_;')
        fallback_guard = ('const bool fallbackSuccess = matchingFullPreset && fx.known && fx.modelName == sentFxModelName_ &&\n'
                          '            fx.enabled == sentFxDesiredEnabled_ && fx.enabled != fxEnabledBeforeRequest_;')
        self.assertIn(direct_guard, fx)
        self.assertIn('if (directSuccess) {\n            confirmFx("FX_ONOFF", "FX_ONOFF");', fx)
        self.assertIn('confirmFx("FX_ONOFF", "FX_ONOFF")', fx)
        self.assertIn(fallback_guard, fx)
        self.assertIn('} else if (fallbackSuccess) {\n            confirmFx("full_preset", "full preset response");', fx)
        self.assertIn('confirmFx("full_preset", "full preset response")', fx)

        completion = fx.split('const auto confirmFx =', 1)[1].split('\n        };', 1)[0]
        self.assertIn('confirmFxRequest(', completion)
        self.assertIn('state_.confirmFxToggleRequest(slot)', completion)
        self.assertIn('SparkDataControl::recordControllerFxConfirm()', completion)
        self.assertIn('persistentEventLog.record(PersistentEvent::FxConfirmed, slot, true)', completion)
        self.assertIn('clearFxRequest()', completion)
        self.assertIn('event=fx_confirmed slot=%u msg=%u source=%s elapsed=%lu', completion)
        self.assertIn('Serial.printf("Controller: FX %u (%s) confirmed by %s', completion)
        self.assertLess(completion.index('confirmFxRequest('), completion.index('event=fx_confirmed'))
        self.assertLess(completion.index('event=fx_confirmed'), completion.index('Serial.printf('))

    def test_full_result_success_is_logged_once_after_bookkeeping(self):
        source = (ROOT / 'src/controller/ControllerActions.cpp').read_text()
        fx = source.split('if (sentFxSlot_ != kNoFxSlot) {', 1)[1].split(
            'if (presetTargets_.queued() == 0) {', 1)[0]
        result = 'event=fx_full_result slot=%u msg=%u match=%u ack=%u known=%u enabled=%u desired=%u chain=%u elapsed=%lu'
        self.assertEqual(fx.count(result), 1)
        self.assertEqual(fx.count('traceFullResult();'), 3)
        self.assertIn('const auto traceFullResult = [=] {', fx)
        self.assertIn('const uint8_t fullResultSlot = sentFxSlot_;', fx)
        self.assertIn('const uint8_t fullResultMessage = SparkDataControl::fullPresetObservationMessageNumber();', fx)
        self.assertLess(fx.index(result), fx.index('if (fullPresetObservedAfterSend &&'))
        cancel = fx.split('if (fullPresetObservedAfterSend &&', 1)[1].split('return;', 1)[0]
        self.assertLess(cancel.index('traceFullResult();'), cancel.index('cancelFxRequest('))
        self.assertIn('if (!directSuccess && !fallbackSuccess) traceFullResult();', fx)
        completion = fx.split('const auto confirmFx =', 1)[1].split('\n        };', 1)[0]
        logger = completion.split('[&, elapsed] {', 1)[1]
        self.assertLess(logger.index('traceFullResult();'), logger.index('event=fx_confirmed'))
        self.assertLess(completion.index('clearFxRequest();'), completion.index('traceFullResult();'))
        self.assertLess(fx.index('if (!directSuccess && !fallbackSuccess) traceFullResult();'), fx.index('if (directSuccess) {'))


if __name__ == '__main__':
    unittest.main()
