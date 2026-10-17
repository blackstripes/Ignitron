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
    // Slot 1 is index 0. Empty means unknown; validated names remain last-known
    // during same-amp cache refetch, not across disconnects or identity changes.
    // Never substitute the active preset's name for an unrelated hardware slot.
    std::array<std::string, 8> hardwarePresetNames;
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
    // Verified local switch number while the new full FX chain is still syncing.
    // Not a second number authority; valid only on this link and for this number.
    uint8_t locallyVerifiedHardwarePreset = 0;
    uint8_t pendingHardwarePreset = 0;
    bool presetActionFailed = false;
    uint8_t failedHardwarePreset = 0;
#ifdef PANELAN_PRESET_TRACE
    uint32_t miniActionId = 0;
    uint32_t miniInputAtMs = 0;
    uint8_t miniActionTarget = 0; // one-based preset, zero-based FX slot
    bool miniActionFx = false;
    bool miniActionConfirmed = false;
    bool miniActionDesiredEnabled = false;
#endif
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
    // A new hardware number invalidates the old chain until a matching full
    // preset response (or a subsequent startup-sync retry) is observed.
    void invalidatePresetData();
    void beginHardwarePresetRequest(uint8_t preset);
    // Only clear the presentation if it still represents this command; a
    // newer deferred intent may already own the pending slot.
    void confirmHardwarePresetRequest(uint8_t preset, bool numberVerified = false);
    void failHardwarePresetRequest();
    void beginFxToggleRequest(uint8_t slot, bool desiredEnabled);
    void confirmFxToggleRequest(uint8_t slot);
    void failFxToggleRequest(uint8_t slot);
    void beginLooperRequest();
    void confirmLooperRequest();
    void failLooperRequest();
    void armLooperClear();
    void disarmLooperClear();
#ifdef PANELAN_PRESET_TRACE
    void traceMiniAction(uint32_t id, uint32_t inputAt, uint8_t target, bool fx, bool confirmed,
                         bool desiredEnabled = false);
#endif

private:
    ControllerSnapshot snapshot_;
    bool wasLinkEstablished_ = false;
    uint32_t fullPresetObservationRevisionAtLink_ = 0;
    bool fullPresetObservedForLink_ = false;
    uint8_t expectedStartupFullPresetMessageNumber_ = 0;
    uint32_t lastLooperStatusRevision_ = 0;
    uint32_t lastLooperSettingsRevision_ = 0;
    uint32_t lastLooperCommandRevision_ = 0;
    uint32_t presetFailedAtMs_ = 0;
    std::array<uint32_t, 6> fxFailedAtMs_{};

    void publishIfChanged(const ControllerSnapshot &next);
};
