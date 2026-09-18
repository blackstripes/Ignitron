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
    void beginHardwarePresetRequest(uint8_t preset);
    void confirmHardwarePresetRequest();
    void failHardwarePresetRequest();
    void beginFxToggleRequest(uint8_t slot, bool desiredEnabled);
    void confirmFxToggleRequest(uint8_t slot);
    void failFxToggleRequest(uint8_t slot);

private:
    ControllerSnapshot snapshot_;
    bool wasLinkEstablished_ = false;

    void publishIfChanged(const ControllerSnapshot &next);
};
