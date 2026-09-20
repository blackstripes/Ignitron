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
    // Entry is confirmed by Spark TUNER_ON. Spark 2 does not reliably emit
    // TUNER_OFF for a native exit, so exit uses the established preset-mode
    // transition and fresh tuner output can still reassert amp ownership.
    bool requestTuner(bool on);
    // A newer explicit navigation choice wins over an in-flight tuner entry.
    void cancelTunerEntry();
    bool requestLooperRecordDub();
    bool requestLooperPlayStop();
    bool requestLooperPlay();
    bool requestLooperStop();
    bool requestLooperUndoRedo();
    // First tap arms a short-lived destructive action; only the second sends DELETE.
    bool requestLooperClear();
    // Leaving the page or choosing another action cancels destructive intent.
    void cancelLooperClear();
    void process(SparkDataControl &dataControl);

private:
    static constexpr uint32_t kPresetTimeoutMs = 5000;
    static constexpr uint32_t kFxTimeoutMs = 5000;
    static constexpr uint32_t kTunerTimeoutMs = 3000;
    static constexpr uint32_t kLooperTimeoutMs = 5000;
    static constexpr uint32_t kLooperClearArmMs = 3000;
    static constexpr uint8_t kNoFxSlot = 0xFF;
    ControllerState &state_;
    uint8_t queuedPreset_ = 0;
    uint8_t sentPreset_ = 0;
    uint8_t presetBeforeRequest_ = 0;
    uint32_t sentAtMs_ = 0;
    uint32_t sentAfterAckRevision_ = 0;
    bool awaitingConfirmationQuery_ = false;
    bool awaitingPresetFullResponse_ = false;
    uint32_t presetFullObservationRevisionBeforeQuery_ = 0;
    uint8_t presetFullQueryMessageNumber_ = 0;
    bool currentPresetQueryIssued_ = false;
    uint32_t currentPresetQueryAtMs_ = 0;
    // This is independent of preset-action verification. It establishes the
    // initial complete preset observation required for the current BLE link.
    bool startupFullPresetQueryIssued_ = false;
    uint32_t startupFullPresetQueryAtMs_ = 0;
    uint8_t startupFullPresetQueryMessageNumber_ = 0;

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

    bool queuedTunerRequest_ = false;
    bool tunerRequestSent_ = false;
    bool queuedTunerEnabled_ = false;
    bool tunerRequestEnabled_ = false;
    bool tunerEntryCancelRequested_ = false;
    uint32_t tunerRequestSentAtMs_ = 0;
    enum class LooperAction : uint8_t { None, RecordDub, PlayStop, Play, Stop, UndoRedo, Clear };
    LooperAction queuedLooperAction_ = LooperAction::None;
    LooperAction sentLooperAction_ = LooperAction::None;
    uint32_t looperSentAtMs_ = 0;
    uint32_t looperSyncRequestedAtMs_ = 0;
    uint32_t looperCommandRevisionBeforeRequest_ = 0;
    uint32_t looperStatusRevisionBeforeRequest_ = 0;

    bool hasPendingFxOperation() const;
    void cancelFxRequest(ControllerState &state, SparkDataControl *dataControl, bool refresh,
                         const char *reason);
    void clearFxRequest();
    bool canRequestLooper() const;
    bool queueLooperAction(LooperAction action);
};
