#include "controller/ControllerState.h"

#include "SparkDataControl.h"
#include "SparkPresetControl.h"
#include "SparkStatus.h"

#include <Arduino.h>

namespace {

constexpr uint8_t kFxSlotCount = 6;
constexpr uint8_t kPedalIndices[kFxSlotCount] = {0, 1, 2, 4, 5, 6};
constexpr const char *kFxLabels[kFxSlotCount] = {"GATE", "COMP", "DRIVE", "MOD", "DELAY", "REVERB"};
// Tuner samples are a stream, not a durable measurement. A bounded window
// makes an interrupted stream visibly become LISTENING rather than leaving a
// stale note on the performance display.
constexpr uint32_t kTunerSampleFreshMs = 1500;

std::string makeFxChainIdentity(const Preset &preset) {
    // UUID is the strongest identity supplied by a full preset. Older/cache
    // paths can omit it, so include the observed slot models as a stable
    // fallback rather than trusting the display name alone. The hardware
    // preset number is deliberately excluded: NEO Core can transiently
    // report it as unknown during an otherwise unchanged FX command.
    std::string identity = preset.uuid.empty() ? "preset:" + preset.name : "uuid:" + preset.uuid;
    for (uint8_t pedalIndex : kPedalIndices) {
        identity += "|";
        if (preset.pedals.size() > pedalIndex) {
            identity += preset.pedals[pedalIndex].name;
        }
    }
    return identity;
}

} // namespace

void ControllerState::refreshFromSpark(SparkDataControl &dataControl) {
    ControllerSnapshot next = snapshot_;
    const bool linkEstablished = SparkDataControl::isAmpConnected();

    if (!linkEstablished) {
        next.connectionPhase = wasLinkEstablished_
                                   ? ControllerConnectionPhase::Reconnecting
                                   : ControllerConnectionPhase::Scanning;
        next.sparkStateStale = true;
        next.identityKnown = false;
        // Tuner mode is Spark-owned. A dropped link cannot leave the UI in a
        // falsely active/muted-looking tuner surface; retain at most the last
        // sample as context, but it is never fresh without the link.
        next.tunerActive = false;
        next.tunerSampleFresh = false;
        if (next.pendingHardwarePreset != 0) {
            next.pendingHardwarePreset = 0;
            next.presetActionFailed = true;
        }
        for (ControllerFxSlot &slot : next.fxSlots) {
            if (slot.pending) {
                slot.pending = false;
                slot.pendingDesiredEnabled = false;
                slot.actionFailed = true;
            }
        }
        wasLinkEstablished_ = false;
        publishIfChanged(next);
        return;
    }

    wasLinkEstablished_ = true;
    SparkStatus &status = SparkStatus::getInstance();
    next.ampName = status.ampName();
    next.ampSerial = status.ampSerialNumber();
    const Preset &activePreset = SparkPresetControl::getInstance().activePreset();
    next.presetName = activePreset.name;
    next.presetDescription = activePreset.description;
    for (size_t i = 0; i < next.fxSlots.size(); ++i) {
        next.fxSlots[i].label = kFxLabels[i];
        next.fxSlots[i].known = activePreset.pedals.size() > kPedalIndices[i];
        next.fxSlots[i].modelName = next.fxSlots[i].known ? activePreset.pedals[kPedalIndices[i]].name : "";
        next.fxSlots[i].enabled = next.fxSlots[i].known && activePreset.pedals[kPedalIndices[i]].isOn;
    }
    const int reportedPreset = status.currentPresetNumber();
    next.confirmedHardwarePreset = reportedPreset >= 1 && reportedPreset <= 4 ? reportedPreset : 0;
    next.fxChainIdentity = makeFxChainIdentity(activePreset);
    next.tunerActive = dataControl.subMode() == SUB_MODE_TUNER;
    next.tunerSampleKnown = status.tunerSampleRevision() != 0;
    next.tunerNote = status.noteString();
    next.tunerOffset = status.noteOffset();
    const uint32_t lastTunerSampleAtMs = status.tunerLastSampleAtMs();
    next.tunerSampleFresh = next.tunerActive && next.tunerSampleKnown &&
                            static_cast<uint32_t>(millis() - lastTunerSampleAtMs) <= kTunerSampleFreshMs;
    next.identityKnown = dataControl.ampNameReceived() && !next.ampName.empty();
    next.connectionPhase = !next.identityKnown
                               ? ControllerConnectionPhase::Identifying
                               : next.confirmedHardwarePreset == 0
                                     ? ControllerConnectionPhase::Syncing
                                     : ControllerConnectionPhase::Ready;
    next.sparkStateStale = next.connectionPhase != ControllerConnectionPhase::Ready;
    publishIfChanged(next);
}

void ControllerState::publishIfChanged(const ControllerSnapshot &next) {
    if (snapshot_.connectionPhase == next.connectionPhase &&
        snapshot_.sparkStateStale == next.sparkStateStale &&
        snapshot_.identityKnown == next.identityKnown &&
        snapshot_.ampName == next.ampName &&
        snapshot_.ampSerial == next.ampSerial &&
        snapshot_.presetName == next.presetName &&
        snapshot_.presetDescription == next.presetDescription &&
        snapshot_.fxChainIdentity == next.fxChainIdentity &&
        snapshot_.tunerActive == next.tunerActive &&
        snapshot_.tunerSampleKnown == next.tunerSampleKnown &&
        snapshot_.tunerSampleFresh == next.tunerSampleFresh &&
        snapshot_.tunerNote == next.tunerNote &&
        snapshot_.tunerOffset == next.tunerOffset &&
        snapshot_.confirmedHardwarePreset == next.confirmedHardwarePreset &&
        snapshot_.pendingHardwarePreset == next.pendingHardwarePreset &&
        snapshot_.presetActionFailed == next.presetActionFailed &&
        snapshot_.fxSlots == next.fxSlots) {
        return;
    }
    snapshot_ = next;
    ++snapshot_.revision;
}

void ControllerState::beginHardwarePresetRequest(uint8_t preset) {
    ControllerSnapshot next = snapshot_;
    next.pendingHardwarePreset = preset;
    next.presetActionFailed = false;
    publishIfChanged(next);
}

void ControllerState::confirmHardwarePresetRequest() {
    ControllerSnapshot next = snapshot_;
    next.pendingHardwarePreset = 0;
    next.presetActionFailed = false;
    publishIfChanged(next);
}

void ControllerState::failHardwarePresetRequest() {
    ControllerSnapshot next = snapshot_;
    next.pendingHardwarePreset = 0;
    next.presetActionFailed = true;
    publishIfChanged(next);
}

void ControllerState::beginFxToggleRequest(uint8_t slot, bool desiredEnabled) {
    if (slot >= snapshot_.fxSlots.size()) {
        return;
    }
    ControllerSnapshot next = snapshot_;
    next.fxSlots[slot].pending = true;
    next.fxSlots[slot].pendingDesiredEnabled = desiredEnabled;
    next.fxSlots[slot].actionFailed = false;
    publishIfChanged(next);
}

void ControllerState::confirmFxToggleRequest(uint8_t slot) {
    if (slot >= snapshot_.fxSlots.size()) {
        return;
    }
    ControllerSnapshot next = snapshot_;
    next.fxSlots[slot].pending = false;
    next.fxSlots[slot].pendingDesiredEnabled = false;
    next.fxSlots[slot].actionFailed = false;
    publishIfChanged(next);
}

void ControllerState::failFxToggleRequest(uint8_t slot) {
    if (slot >= snapshot_.fxSlots.size()) {
        return;
    }
    ControllerSnapshot next = snapshot_;
    next.fxSlots[slot].pending = false;
    next.fxSlots[slot].pendingDesiredEnabled = false;
    next.fxSlots[slot].actionFailed = true;
    publishIfChanged(next);
}
