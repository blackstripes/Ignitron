#pragma once

#include <cstdint>
#include <array>
#include <string>

class SparkDataControl;

// The immutable renderer-facing portion of controller state. Spark remains
// authoritative for its fields; this object only represents their current
// trust level and makes stale state explicit for every UI consumer.
enum class ControllerConnectionPhase : uint8_t {
    Scanning,
    Reconnecting,
    Identifying,
    Syncing,
    Ready,
};

enum class ControllerLooperCapability : uint8_t { Unknown, Unsupported, Verified };
enum class ControllerLooperTransport : uint8_t { Unknown, Empty, Stopped, Recording, Playing, Overdubbing };

struct ControllerFxSlot {
    std::string label;
    // Spark's actual effect-model name for this logical signal-chain slot.
    // Commands must target this value, never a UI label or slot number.
    std::string modelName;
    bool enabled = false;
    bool known = false;
    bool pending = false;
    bool pendingDesiredEnabled = false;
    bool actionFailed = false;

    bool operator==(const ControllerFxSlot &other) const {
        return label == other.label && modelName == other.modelName && enabled == other.enabled &&
               known == other.known && pending == other.pending &&
               pendingDesiredEnabled == other.pendingDesiredEnabled && actionFailed == other.actionFailed;
    }
};

struct ControllerSnapshot {
    ControllerConnectionPhase connectionPhase = ControllerConnectionPhase::Scanning;
    bool sparkStateStale = true;
    bool identityKnown = false;
    // A complete Spark preset response received after the current BLE link was
    // established. Cached preset data from an earlier link is not sufficient
    // to make controls writable.
    bool fullPresetObservedForLink = false;
    std::string ampName;
    std::string ampSerial;
    std::string presetName;
    std::string presetDescription;
    // Identifies the currently observed signal chain. It is deliberately
    // derived from Spark-owned preset data and is used to cancel a pending FX
    // operation if a preset/chain changes underneath it.
    std::string fxChainIdentity;
    std::array<ControllerFxSlot, 6> fxSlots;
    // Spark-owned tuner observations. `tunerSampleFresh` expires on the
    // controller loop; renderers must not treat cached note/offset values as
    // a live pitch reading after that interval.
    bool tunerActive = false;
    bool tunerSampleKnown = false;
    bool tunerSampleFresh = false;
    std::string tunerNote;
    float tunerOffset = 0.0f;
    int tunerOffsetCents = 0;
    // Spark 2 looper fields are observations, never inferred from a touch.
    ControllerLooperCapability looperCapability = ControllerLooperCapability::Unknown;
    ControllerLooperTransport looperTransport = ControllerLooperTransport::Unknown;
    // Fresh looper evidence exists; transport may still be Unknown when a
    // native status reports only loop count. Gate count-based actions on this.
    bool looperKnown = false;
    bool looperStale = true;
    uint8_t looperLoopCount = 0;
    int looperBpm = 0;
    int looperBars = 0;
    bool looperStraight = true;
    bool looperClick = false;
    bool looperSettingsKnown = false;
    bool looperPending = false;
    bool looperActionFailed = false;
    bool looperClearArmed = false;
    uint8_t confirmedHardwarePreset = 0;
    uint8_t pendingHardwarePreset = 0;
    bool presetActionFailed = false;
    uint32_t revision = 0;
};

class ControllerState {
public:
    const ControllerSnapshot &snapshot() const { return snapshot_; }

    // Called only from the Arduino/controller loop, after BLE/protocol work.
    // No BLE callback or renderer may mutate this state.
    void refreshFromSpark(SparkDataControl &dataControl);
    // The startup synchronizer owns the authoritative full-preset request;
    // only its matching response may make this link actionable.
    void expectStartupFullPreset(uint8_t messageNumber);
    void beginHardwarePresetRequest(uint8_t preset);
    void confirmHardwarePresetRequest();
    void failHardwarePresetRequest();
    void beginFxToggleRequest(uint8_t slot, bool desiredEnabled);
    void confirmFxToggleRequest(uint8_t slot);
    void failFxToggleRequest(uint8_t slot);
    void beginLooperRequest();
    void confirmLooperRequest();
    void failLooperRequest();
    void armLooperClear();
    void disarmLooperClear();

private:
    ControllerSnapshot snapshot_;
    bool wasLinkEstablished_ = false;
    uint32_t fullPresetObservationRevisionAtLink_ = 0;
    bool fullPresetObservedForLink_ = false;
    uint8_t expectedStartupFullPresetMessageNumber_ = 0;
    uint32_t lastLooperStatusRevision_ = 0;
    uint32_t lastLooperSettingsRevision_ = 0;
    uint32_t lastLooperCommandRevision_ = 0;

    void publishIfChanged(const ControllerSnapshot &next);
};
