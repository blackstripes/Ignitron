#include "controller/ControllerState.h"

#include "SparkDataControl.h"
#include "SparkPresetControl.h"
#include "SparkStatus.h"

void ControllerState::refreshFromSpark(SparkDataControl &dataControl) {
    ControllerSnapshot next = snapshot_;
    const bool linkEstablished = SparkDataControl::isAmpConnected();

    if (!linkEstablished) {
        next.connectionPhase = wasLinkEstablished_
                                   ? ControllerConnectionPhase::Reconnecting
                                   : ControllerConnectionPhase::Scanning;
        next.sparkStateStale = true;
        next.identityKnown = false;
        if (next.pendingHardwarePreset != 0) {
            next.pendingHardwarePreset = 0;
            next.presetActionFailed = true;
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
    static constexpr const char *kFxLabels[] = {"GATE", "COMP", "DRIVE", "MOD", "DELAY", "REVERB"};
    static constexpr uint8_t kPedalIndices[] = {0, 1, 2, 4, 5, 6};
    for (size_t i = 0; i < next.fxSlots.size(); ++i) {
        next.fxSlots[i].label = kFxLabels[i];
        next.fxSlots[i].known = activePreset.pedals.size() > kPedalIndices[i];
        next.fxSlots[i].enabled = next.fxSlots[i].known && activePreset.pedals[kPedalIndices[i]].isOn;
    }
    const int reportedPreset = status.currentPresetNumber();
    next.confirmedHardwarePreset = reportedPreset >= 1 && reportedPreset <= 4 ? reportedPreset : 0;
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
