#pragma once

#include <cstdint>
#include <string>

class ControllerState;
class SparkDataControl;

// Sole mutation entrypoint for the initial UI action. It serializes one
// hardware-preset request and keeps the confirmed Spark value separate from
// the UI's pending intent.
class ControllerActions {
public:
    explicit ControllerActions(ControllerState &state) : state_(state) {}

    bool requestHardwarePreset(uint8_t preset);
    // Queues one model-specific bypass/on-off request. Confirmation is based
    // exclusively on a fresh Spark-owned slot observation, never an ACK.
    bool requestFxToggle(uint8_t slot);
    void process(SparkDataControl &dataControl);

private:
    static constexpr uint32_t kPresetTimeoutMs = 5000;
    static constexpr uint32_t kFxTimeoutMs = 5000;
    static constexpr uint8_t kNoFxSlot = 0xFF;
    ControllerState &state_;
    uint8_t queuedPreset_ = 0;
    uint8_t sentPreset_ = 0;
    uint8_t presetBeforeRequest_ = 0;
    uint32_t sentAtMs_ = 0;
    uint32_t sentAfterAckRevision_ = 0;
    bool awaitingConfirmationQuery_ = false;
    bool currentPresetQueryIssued_ = false;
    uint32_t currentPresetQueryAtMs_ = 0;

    uint8_t queuedFxSlot_ = kNoFxSlot;
    uint8_t sentFxSlot_ = kNoFxSlot;
    bool queuedFxDesiredEnabled_ = false;
    bool sentFxDesiredEnabled_ = false;
    bool fxEnabledBeforeRequest_ = false;
    std::string queuedFxModelName_;
    std::string sentFxModelName_;
    std::string fxChainIdentityBeforeRequest_;
    uint32_t fxSentAtMs_ = 0;
    uint32_t fxModelObservationRevisionBeforeRequest_ = 0;
    uint32_t fxFullPresetObservationRevisionBeforeRequest_ = 0;
    uint32_t fxSentAfterAckRevision_ = 0;
    uint8_t sentFxMessageNumber_ = 0;
    bool fxFullPresetQueryIssued_ = false;

    bool hasPendingFxOperation() const;
    void cancelFxRequest(ControllerState &state, SparkDataControl *dataControl, bool refresh,
                         const char *reason);
    void clearFxRequest();
};
