#pragma once

#include <cstdint>

class ControllerState;
class SparkDataControl;

// Sole mutation entrypoint for the initial UI action. It serializes one
// hardware-preset request and keeps the confirmed Spark value separate from
// the UI's pending intent.
class ControllerActions {
public:
    explicit ControllerActions(ControllerState &state) : state_(state) {}

    bool requestHardwarePreset(uint8_t preset);
    void process(SparkDataControl &dataControl);

private:
    static constexpr uint32_t kPresetTimeoutMs = 5000;
    ControllerState &state_;
    uint8_t queuedPreset_ = 0;
    uint8_t sentPreset_ = 0;
    uint8_t presetBeforeRequest_ = 0;
    uint32_t sentAtMs_ = 0;
    bool currentPresetQueryIssued_ = false;
    uint32_t currentPresetQueryAtMs_ = 0;
};
