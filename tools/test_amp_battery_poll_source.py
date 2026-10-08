"""Check production wiring of the host-tested scheduler and loop priority."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BatteryPollWiring(unittest.TestCase):
    def test_order_and_guards(self):
        loop = (ROOT / "src/Ignitron.ino").read_text().split("void loop() {")[1]
        self.assertLess(loop.index("spark_dc->checkForUpdates();"),
                        loop.index("controllerState.refreshFromSpark(*spark_dc);", loop.index("spark_dc->checkForUpdates();")))
        actions = loop.index("controllerActions.process(*spark_dc);")
        guard = loop.index("controllerActions.backgroundQueriesAllowed();", actions)
        service_call = loop.index("spark_dc->serviceBackgroundQueries(foregroundReady);")
        self.assertLess(actions, guard)
        self.assertLess(guard, service_call)
        self.assertIn("const bool foregroundReady = true;", loop[guard:service_call])
        self.assertIn("if (operationMode != SPARK_MODE_KEYBOARD) {\n        spark_dc->serviceBackgroundQueries(foregroundReady);", loop)
        header = (ROOT / "src/controller/ControllerActions.h").read_text()
        self.assertIn("bool backgroundQueriesAllowed() const;", header)
        predicate = (ROOT / "src/controller/ControllerActions.cpp").read_text().split(
            "bool ControllerActions::backgroundQueriesAllowed() const {")[1].split("\n}")[0]
        for term in ("state_.snapshot()", "ControllerConnectionPhase::Ready", "fullPresetObservedForLink",
                     "!snapshot.sparkStateStale", "snapshot.pendingHardwarePreset == 0", "!snapshot.presetActionFailed",
                     "sentPreset_ == 0", "presetTargets_.queued() == 0", "presetTargets_.deferred() == 0",
                     "!presetTimeoutReconcile_.needed()", "!awaitingPresetFullResponse_",
                     "(!fullPresetRetry_.attempted() || startupFullPresetQueryIssued_)", "!hasPendingFxOperation()",
                     "!queuedTunerRequest_", "!tunerRequestSent_", "queuedLooperAction_ == LooperAction::None",
                     "sentLooperAction_ == LooperAction::None"):
            self.assertIn(term, predicate)
        source = (ROOT / "src/SparkDataControl.cpp").read_text()
        updates = source.split("void SparkDataControl::checkForUpdates() {")[1].split("void SparkDataControl::serviceBackgroundQueries(bool foregroundReady) {")[0]
        self.assertNotIn("getAmpStatus", updates)
        service = source.split("void SparkDataControl::serviceBackgroundQueries(bool foregroundReady) {")[1].split("void SparkDataControl::processSparkData(")[0]
        self.assertIn("operationMode_ == SPARK_MODE_KEYBOARD", service)
        self.assertNotIn("operationMode_ != SPARK_MODE_APP", service)
        self.assertIn("operationMode_ == SPARK_MODE_AMP ? isAppConnected() : isAmpConnected()", service)
        self.assertIn("triggerCommand(currentMsg)", service)
        self.assertIn("ampBatteryPoll_.service(millis(), connected, foregroundReady", service)
        self.assertIn("reason=controller_work_pending", service)
        self.assertIn("ampBatteryPoll_.reset();", source.split("void SparkDataControl::resetStatus() {")[1].split("void SparkDataControl::checkForUpdates() {")[0])


if __name__ == "__main__":
    unittest.main()
