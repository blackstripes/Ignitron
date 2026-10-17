"""Focused guards for main LVGL changed-only rendering and trace completion."""
from pathlib import Path
import unittest

UI = (Path(__file__).resolve().parents[1] / 'src/PanelLanLVGLUI.cpp').read_text()
SRC = Path(__file__).resolve().parents[1] / 'src'


class MainActionTest(unittest.TestCase):
    def test_fx_visuals_and_guards(self):
        detail = UI.split('void PanelLanLVGLUI::renderDetailPage(', 1)[1].split(
            'void PanelLanLVGLUI::reconcileExternalTuner(', 1)[0]
        home = UI.split('void PanelLanLVGLUI::renderStatus(', 1)[1].split(
            'void PanelLanLVGLUI::traceMain(', 1)[0]
        for part in (detail, home):
            self.assertIn('MiniActionPresentation::fxEnabled(slot)', part)
            self.assertNotIn('lv_obj_invalidate(fxIcons_[fx])', part)
            self.assertNotIn('lv_obj_invalidate(detailTileIcons_[fx])', part)
            self.assertIn('setColor(', part)
            self.assertIn('setText(', part)
        self.assertIn('if (std::strcmp(lv_label_get_text(obj), text) == 0) return false;', UI)
        self.assertIn('if (lv_color_eq(old, color)) return false;', UI)
        self.assertIn('state = "SYNC";', detail)
        self.assertIn('pending ? "*" : "!"', detail)
        self.assertIn('const bool failed = !pending && slot.actionFailed;', detail)
        self.assertIn('const bool marked = pending || failed;', detail)
        self.assertIn('!current ? "?"', home)
        self.assertIn('const bool failed = slot.actionFailed && !(current && slot.pending);', home)
        self.assertIn('show(fxErrorLabels_[fx], failed);', home)
        self.assertIn('snapshot.pendingHardwarePreset == preset ? 0xFFD06A', home)
        self.assertIn('snapshot.failedHardwarePreset == preset', home)

    def test_ready_and_target_area_before_logging(self):
        flush = UI.split('void PanelLanLVGLUI::flushDisplay(', 1)[1].split(
            'void PanelLanLVGLUI::readTouch(', 1)[0]
        self.assertIn('trace.refreshed = true;', flush)
        self.assertNotIn('Serial.', flush)
        callback = UI.split('LV_EVENT_REFR_READY', 1)[0]
        self.assertIn('trace.dirty && trace.refreshed', callback)
        self.assertIn('mainTraceMatches(uiInstance->latestSnapshot_)', callback)
        self.assertIn('trace.completedAt = millis();', callback)
        output = UI.split('void PanelLanLVGLUI::printMainTrace()', 1)[1]
        self.assertIn('if (!trace.ready) return;', output)
        update = UI.split('void PanelLanLVGLUI::update(', 1)[1]
        self.assertIn('lv_timer_handler();', update)
        self.assertNotIn('printMainTrace();', update)
        self.assertNotIn('drainMainTrace();', update)
        self.assertIn('void PanelLanLVGLUI::drainMainTrace() {\n    printMainTrace();\n}', UI)
        header = (SRC / 'PanelLanLVGLUI.h').read_text().split('public:', 1)[1].split('private:', 1)[0]
        self.assertIn('#ifdef PANELAN_PRESET_TRACE\n    // Called after the mini display', header)
        self.assertIn('void drainMainTrace();', header)
        screens = (SRC / 'Ignitron.ino').read_text().split('void updatePanelLanScreens() {', 1)[1].split('\n}', 1)[0]
        self.assertLess(screens.index('panelLanDisplay.update(snapshot);'),
                        screens.index('panelLanMiniDisplay.update(snapshot,'))
        self.assertLess(screens.index('panelLanMiniDisplay.update(snapshot,'),
                        screens.index('panelLanDisplay.drainMainTrace();'))
        self.assertIn('#ifdef PANELAN_PRESET_TRACE\n    panelLanDisplay.drainMainTrace();\n#endif', screens)
        self.assertIn('MiniActionPresentation::fxConfirmedDirection(s, s.miniActionTarget, s.miniActionDesiredEnabled)', UI)
        self.assertIn('MiniActionPresentation::presetConfirmed(s, s.miniActionTarget)', UI)

    def test_trace_retained_until_ready_or_superseded_and_occlusion_checked(self):
        status = UI.split('void PanelLanLVGLUI::renderStatus(', 1)[1].split(
            'bool PanelLanLVGLUI::mainTraceMatches(', 1)[0]
        self.assertIn('if (mainTrace_.dirty && !mainTraceMatches(snapshot)) mainTrace_ = {};', status)
        self.assertNotIn('\n    mainTrace_ = {};\n#endif', status.split('latestSnapshot_ = snapshot;', 1)[0])
        match = UI.split('bool PanelLanLVGLUI::mainTraceMatches(', 1)[1].split(
            'void PanelLanLVGLUI::traceMain(', 1)[0]
        for condition in ('trace.id != s.miniActionId', 'trace.final != s.miniActionConfirmed',
                          'trace.target != s.miniActionTarget', 'trace.inputAt != s.miniInputAtMs',
                          'lv_obj_has_flag(presetPicker_, LV_OBJ_FLAG_HIDDEN)',
                          'activeScreen_ != Screen::Fx', 'lv_obj_has_flag(detailPage_, LV_OBJ_FLAG_HIDDEN)'):
            self.assertIn(condition, match)
        self.assertIn('if (mainTrace_.dirty && mainTraceMatches(s)) return;', UI)
        self.assertIn('mainFirstId_ = trace.id;', UI)
        self.assertIn('mainFinalId_ = trace.id;', UI)
        self.assertIn('lv_obj_has_flag(presetPicker_, LV_OBJ_FLAG_HIDDEN))\n            traceMain(snapshot, tile, dirty);', status)
        detail = UI.split('void PanelLanLVGLUI::renderDetailPage(', 1)[1].split(
            'void PanelLanLVGLUI::reconcileExternalTuner(', 1)[0]
        self.assertIn('!lv_obj_has_flag(detailPage_, LV_OBJ_FLAG_HIDDEN))\n                traceMain(snapshot, detailTiles_[fx], dirty);', detail)


if __name__ == '__main__':
    unittest.main()
