#pragma once

#include <cstdint>
#include <string>
#include "HardwarePresetScan.h"
#include "PresetTargetQueue.h"
#include "PresetTimeoutReconcile.h"

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
    // Call on every disconnected headless tick (before its early return).
    // Idempotent; never sends a Spark command.
    void onAmpDisconnected();
    void process(SparkDataControl &dataControl);

private:
    static constexpr uint32_t kPresetTimeoutMs = 5000;
    static constexpr uint32_t kFxTimeoutMs = 5000;
    static constexpr uint32_t kTunerTimeoutMs = 3000;
    static constexpr uint32_t kLooperTimeoutMs = 5000;
    static constexpr uint32_t kLooperClearArmMs = 3000;
    static constexpr uint8_t kNoFxSlot = 0xFF;
    ControllerState &state_;
    PresetTargetQueue presetTargets_;
    PresetTimeoutReconcile presetTimeoutReconcile_;
    uint32_t presetReconcileQueryAtMs_ = 0;
    bool presetReconcileQueryAttempted_ = false;
    uint8_t reconciledPresetNumber_ = 0;
    uint8_t sentPreset_ = 0;
    uint8_t sentPresetMessageNumber_ = 0;
    uint8_t presetBeforeRequest_ = 0;
    uint32_t sentAtMs_ = 0;
    uint32_t sentAfterAckRevision_ = 0;
    uint32_t sentAfterNumberRevision_ = 0;
    uint8_t confirmationQueryMessageNumber_ = 0;
    bool awaitingConfirmationQuery_ = false;
    // Refresh-only after number confirmation; never owns preset action status.
    bool awaitingPresetFullResponse_ = false;
    uint32_t presetFullObservationRevisionBeforeQuery_ = 0;
    uint8_t presetFullQueryMessageNumber_ = 0;
    uint8_t presetFullTarget_ = 0;
#ifdef PANELAN_PRESET_TRACE
    uint32_t presetTraceId_ = 0;
    uint32_t presetTraceNextId_ = 0;
    uint32_t presetTraceDeferredId_ = 0;
    uint32_t presetTraceDeferredAtMs_ = 0;
    uint32_t presetTraceStartedAtMs_ = 0;
    uint32_t presetTraceFullRevision_ = 0;
    uint8_t presetTraceObserved_ = 0;
    uint8_t presetTraceFailedTarget_ = 0;
    uint32_t presetTraceFailedId_ = 0;
    uint8_t presetTraceFailedFullMsg_ = 0;
    uint8_t presetTraceFailedFullTarget_ = 0;
    uint32_t presetTraceFailedFullId_ = 0;
    uint32_t presetTraceFailedFullRevision_ = 0;
    uint8_t presetTracePhase_ = 0;
#endif
    bool currentPresetQueryIssued_ = false;
    uint32_t currentPresetQueryAtMs_ = 0;
    // This is independent of preset-action verification. It establishes the
    // initial complete preset observation required for the current BLE link.
    bool startupFullPresetQueryIssued_ = false;
    uint32_t startupFullPresetQueryAtMs_ = 0;
    uint8_t startupFullPresetQueryMessageNumber_ = 0;
    HardwarePresetScan cacheScan_;
    bool cacheMetadataRequested_ = false;
    uint32_t cacheMetadataAtMs_ = 0;
    uint32_t cacheGeneration_ = 0;

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
    void processHardwareNameCache(SparkDataControl &dataControl);
    bool canRequestLooper() const;
    bool queueLooperAction(LooperAction action);
};
