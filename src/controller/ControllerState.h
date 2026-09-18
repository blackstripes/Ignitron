#pragma once

#include <cstdint>
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

struct ControllerSnapshot {
    ControllerConnectionPhase connectionPhase = ControllerConnectionPhase::Scanning;
    bool sparkStateStale = true;
    bool identityKnown = false;
    std::string ampName;
    std::string ampSerial;
    uint32_t revision = 0;
};

class ControllerState {
public:
    const ControllerSnapshot &snapshot() const { return snapshot_; }

    // Called only from the Arduino/controller loop, after BLE/protocol work.
    // No BLE callback or renderer may mutate this state.
    void refreshFromSpark(SparkDataControl &dataControl);

private:
    ControllerSnapshot snapshot_;
    bool wasLinkEstablished_ = false;

    void publishIfChanged(const ControllerSnapshot &next);
};
