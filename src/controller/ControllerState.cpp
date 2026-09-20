#include "controller/ControllerState.h"

#include "SparkDataControl.h"
#include "SparkPresetControl.h"
#include "SparkStatus.h"
#include "PersistentEventLog.h"

#include <Arduino.h>

namespace {

constexpr uint8_t kFxSlotCount = 6;
constexpr uint8_t kPedalIndices[kFxSlotCount] = {0, 1, 2, 4, 5, 6};
constexpr const char *kFxLabels[kFxSlotCount] = {"GATE", "COMP", "DRIVE", "MOD", "DELAY", "REVERB"};
// Tuner samples are a stream, not a durable measurement. A bounded window
// makes an interrupted stream visibly become LISTENING rather than leaving a
// stale note on the performance display.
constexpr uint32_t kTunerSampleFreshMs = 1500;
// Hardware probing established that the Spark 2 model exposes the native
// looper protocol. The serial number identifies the test unit in the evidence
// log, but must not become a product capability gate for other Spark 2 amps.

ControllerLooperTransport transportFromObservedCommand(byte command,
                                                        ControllerLooperTransport current) {
    switch (command) {
    case SPK_LOOPER_CMD_REC: return ControllerLooperTransport::Recording;
    case SPK_LOOPER_CMD_DUB: return ControllerLooperTransport::Overdubbing;
    case SPK_LOOPER_CMD_PLAY: return ControllerLooperTransport::Playing;
    case SPK_LOOPER_CMD_STOP:
    case SPK_LOOPER_CMD_STOP_REC:
    case SPK_LOOPER_CMD_STOP_DUB: return ControllerLooperTransport::Stopped;
    case SPK_LOOPER_CMD_DELETE: return ControllerLooperTransport::Empty;
    default: return current;
    }
}

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
        next.fullPresetObservedForLink = false;
        // Tuner mode is Spark-owned. A dropped link cannot leave the UI in a
        // falsely active/muted-looking tuner surface; retain at most the last
        // sample as context, but it is never fresh without the link.
        next.tunerActive = false;
        next.tunerSampleFresh = false;
        next.looperStale = true;
        next.looperKnown = false;
        next.looperTransport = ControllerLooperTransport::Unknown;
        next.looperSettingsKnown = false;
        next.looperPending = false;
        next.looperClearArmed = false;
        lastLooperStatusRevision_ = 0;
        lastLooperSettingsRevision_ = 0;
        lastLooperCommandRevision_ = 0;
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
        fullPresetObservationRevisionAtLink_ = 0;
        fullPresetObservedForLink_ = false;
        expectedStartupFullPresetMessageNumber_ = 0;
        publishIfChanged(next);
        return;
    }

    if (!wasLinkEstablished_) {
        // A full preset retained by SparkPresetControl may belong to the prior
        // BLE session. Capture the protocol generation at this link boundary
        // and require a later complete response before declaring readiness.
        fullPresetObservationRevisionAtLink_ = SparkDataControl::fullPresetObservationRevision();
        fullPresetObservedForLink_ = false;
    }
    wasLinkEstablished_ = true;
    if (expectedStartupFullPresetMessageNumber_ != 0 &&
        SparkDataControl::fullPresetObservationRevision() != fullPresetObservationRevisionAtLink_ &&
        SparkDataControl::fullPresetObservationMessageNumber() == expectedStartupFullPresetMessageNumber_) {
        fullPresetObservedForLink_ = true;
    }
    next.fullPresetObservedForLink = fullPresetObservedForLink_;
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
    const int maxHardwarePreset = SparkPresetControl::getInstance().numberOfHWBanks() * PRESETS_PER_BANK;
    next.confirmedHardwarePreset = reportedPreset >= 1 && reportedPreset <= maxHardwarePreset ? reportedPreset : 0;
    next.fxChainIdentity = makeFxChainIdentity(activePreset);
    next.tunerActive = dataControl.subMode() == SUB_MODE_TUNER;
    next.tunerSampleKnown = status.tunerSampleRevision() != 0;
    next.tunerNote = status.noteString();
    next.tunerOffset = status.noteOffset();
    next.tunerOffsetCents = status.noteOffsetCents();
    const uint32_t lastTunerSampleAtMs = status.tunerLastSampleAtMs();
    next.tunerSampleFresh = next.tunerActive && next.tunerSampleKnown &&
                            static_cast<uint32_t>(millis() - lastTunerSampleAtMs) <= kTunerSampleFreshMs;
    next.identityKnown = dataControl.ampNameReceived() && !next.ampName.empty();
    next.looperCapability = next.identityKnown && next.ampName.find("Spark 2") != std::string::npos
                                ? ControllerLooperCapability::Verified
                                : next.identityKnown ? ControllerLooperCapability::Unsupported
                                                     : ControllerLooperCapability::Unknown;
    if (next.looperCapability == ControllerLooperCapability::Verified) {
        SparkStatus &looperStatus = SparkStatus::getInstance();
        const uint32_t settingsRevision = SparkDataControl::looperSettingsObservationRevision();
        if (settingsRevision != 0 && settingsRevision != lastLooperSettingsRevision_) {
            const LooperSetting setting = looperStatus.currentLooperSetting();
            next.looperBpm = setting.bpm;
            next.looperBars = setting.bars;
            next.looperStraight = setting.count == 0x04;
            next.looperClick = setting.click;
            next.looperSettingsKnown = true;
            lastLooperSettingsRevision_ = settingsRevision;
        }
        const uint32_t statusRevision = SparkDataControl::looperStatusObservationRevision();
        if (statusRevision != 0 && statusRevision != lastLooperStatusRevision_) {
            next.looperLoopCount = static_cast<uint8_t>(looperStatus.numberOfLoops());
            next.looperKnown = true;
            next.looperStale = false;
            if (next.looperLoopCount == 0 && next.looperTransport == ControllerLooperTransport::Unknown) {
                next.looperTransport = ControllerLooperTransport::Empty;
            }
            lastLooperStatusRevision_ = statusRevision;
        }
        const uint32_t commandRevision = SparkDataControl::looperCommandObservationRevision();
        if (commandRevision != 0 && commandRevision != lastLooperCommandRevision_) {
            next.looperTransport = transportFromObservedCommand(looperStatus.lastLooperCommand(), next.looperTransport);
            // An UNDO/REDO or other non-transport notification must not erase
            // the fresh loop-count evidence supplied by a status response.
            next.looperKnown = next.looperKnown || next.looperTransport != ControllerLooperTransport::Unknown;
            next.looperStale = false;
            lastLooperCommandRevision_ = commandRevision;
        }
    } else {
        next.looperKnown = false;
        next.looperStale = true;
        next.looperSettingsKnown = false;
        next.looperPending = false;
        next.looperClearArmed = false;
    }
    next.connectionPhase = !next.identityKnown
                               ? ControllerConnectionPhase::Identifying
                               : next.confirmedHardwarePreset == 0 || !next.fullPresetObservedForLink
                                      ? ControllerConnectionPhase::Syncing
                                      : ControllerConnectionPhase::Ready;
    next.sparkStateStale = next.connectionPhase != ControllerConnectionPhase::Ready;
    publishIfChanged(next);
}

void ControllerState::expectStartupFullPreset(uint8_t messageNumber) {
    expectedStartupFullPresetMessageNumber_ = messageNumber;
}

void ControllerState::publishIfChanged(const ControllerSnapshot &next) {
    if (snapshot_.connectionPhase != next.connectionPhase) persistentEventLog.record(PersistentEvent::SyncPhase, static_cast<uint16_t>(next.connectionPhase), true);
    if (snapshot_.connectionPhase == next.connectionPhase &&
        snapshot_.sparkStateStale == next.sparkStateStale &&
        snapshot_.identityKnown == next.identityKnown &&
        snapshot_.fullPresetObservedForLink == next.fullPresetObservedForLink &&
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
        snapshot_.tunerOffsetCents == next.tunerOffsetCents &&
        snapshot_.looperCapability == next.looperCapability &&
        snapshot_.looperTransport == next.looperTransport &&
        snapshot_.looperKnown == next.looperKnown && snapshot_.looperStale == next.looperStale &&
        snapshot_.looperLoopCount == next.looperLoopCount && snapshot_.looperBpm == next.looperBpm &&
        snapshot_.looperBars == next.looperBars && snapshot_.looperStraight == next.looperStraight &&
        snapshot_.looperClick == next.looperClick && snapshot_.looperSettingsKnown == next.looperSettingsKnown &&
        snapshot_.looperPending == next.looperPending && snapshot_.looperActionFailed == next.looperActionFailed &&
        snapshot_.looperClearArmed == next.looperClearArmed &&
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

void ControllerState::beginLooperRequest() { ControllerSnapshot next = snapshot_; next.looperPending = true; next.looperActionFailed = false; publishIfChanged(next); }
void ControllerState::confirmLooperRequest() { ControllerSnapshot next = snapshot_; next.looperPending = false; next.looperActionFailed = false; publishIfChanged(next); }
void ControllerState::failLooperRequest() { ControllerSnapshot next = snapshot_; next.looperPending = false; next.looperActionFailed = true; publishIfChanged(next); }
void ControllerState::armLooperClear() { ControllerSnapshot next = snapshot_; next.looperClearArmed = true; publishIfChanged(next); }
void ControllerState::disarmLooperClear() { ControllerSnapshot next = snapshot_; next.looperClearArmed = false; publishIfChanged(next); }
