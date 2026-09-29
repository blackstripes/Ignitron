"""Source contract for the headless early-return disconnect trace path."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DisconnectTraceSourceTest(unittest.TestCase):
    def test_direct_and_process_disconnect_share_one_shot_marker(self):
        actions = (ROOT / "src/controller/ControllerActions.cpp").read_text()
        disconnect = actions.split("void ControllerActions::onAmpDisconnected() {", 1)[1].split(
            "void ControllerActions::process(", 1)[0]
        process = actions.split("void ControllerActions::process(", 1)[1]
        headless = (ROOT / "src/Ignitron.ino").read_text()

        self.assertIn('if (presetTraceConnectionObserved_) PRESET_TRACE("event=disconnect");', disconnect)
        self.assertIn("presetTraceConnectionObserved_ = false;", disconnect)
        self.assertIn("presetTraceConnectionObserved_ = true;", process)
        self.assertIn("onAmpDisconnected();", process)
        self.assertNotIn('PRESET_TRACE("event=disconnect")', process)
        self.assertEqual(actions.count('PRESET_TRACE("event=disconnect")'), 1)
        self.assertLess(headless.index("controllerActions.onAmpDisconnected();"),
                        headless.index("controllerActions.process("))


if __name__ == "__main__":
    unittest.main()
