#include "controller/ControllerActions.h"

#include "controller/ControllerState.h"
#include "SparkDataControl.h"

bool ControllerActions::requestHardwarePreset(uint8_t preset) {
    const ControllerSnapshot &snapshot = state_.snapshot();
    if (preset < 1 || preset > 4 || snapshot.connectionPhase != ControllerConnectionPhase::Ready ||
        snapshot.sparkStateStale || snapshot.pendingHardwarePreset != 0 || hasPendingFxOperation() ||
        preset == snapshot.confirmedHardwarePreset) {
        return false;
    }
    queuedPreset_ = preset;
    presetBeforeRequest_ = snapshot.confirmedHardwarePreset;
    state_.beginHardwarePresetRequest(preset);
    return true;
}

bool ControllerActions::requestFxToggle(uint8_t slot) {
    const ControllerSnapshot &snapshot = state_.snapshot();
    if (slot >= snapshot.fxSlots.size() || snapshot.connectionPhase != ControllerConnectionPhase::Ready ||
        snapshot.sparkStateStale || snapshot.pendingHardwarePreset != 0 || sentPreset_ != 0 ||
        queuedPreset_ != 0 || hasPendingFxOperation()) {
        return false;
    }

    const ControllerFxSlot &fx = snapshot.fxSlots[slot];
    if (!fx.known || fx.modelName.empty()) {
        return false;
    }

    queuedFxSlot_ = slot;
    queuedFxDesiredEnabled_ = !fx.enabled;
    queuedFxModelName_ = fx.modelName;
    fxEnabledBeforeRequest_ = fx.enabled;
    fxHardwarePresetBeforeRequest_ = snapshot.confirmedHardwarePreset;
    fxChainIdentityBeforeRequest_ = snapshot.fxChainIdentity;
    state_.beginFxToggleRequest(slot, queuedFxDesiredEnabled_);
    return true;
}

bool ControllerActions::hasPendingFxOperation() const {
    return queuedFxSlot_ != kNoFxSlot || sentFxSlot_ != kNoFxSlot;
}

void ControllerActions::cancelFxRequest(ControllerState &state, SparkDataControl *dataControl, bool refresh) {
    const uint8_t slot = sentFxSlot_ != kNoFxSlot ? sentFxSlot_ : queuedFxSlot_;
    if (slot != kNoFxSlot) {
        state.failFxToggleRequest(slot);
    }
    clearFxRequest();
    if (refresh && dataControl != nullptr) {
        dataControl->getCurrentPresetFromSpark();
    }
}

void ControllerActions::clearFxRequest() {
    queuedFxSlot_ = kNoFxSlot;
    sentFxSlot_ = kNoFxSlot;
    queuedFxDesiredEnabled_ = false;
    sentFxDesiredEnabled_ = false;
    queuedFxModelName_.clear();
    sentFxModelName_.clear();
    fxChainIdentityBeforeRequest_.clear();
    fxModelObservationRevisionBeforeRequest_ = 0;
}

void ControllerActions::process(SparkDataControl &dataControl) {
    const ControllerSnapshot &snapshot = state_.snapshot();
    if (snapshot.connectionPhase == ControllerConnectionPhase::Scanning ||
        snapshot.connectionPhase == ControllerConnectionPhase::Reconnecting ||
        snapshot.connectionPhase == ControllerConnectionPhase::Identifying) {
        currentPresetQueryIssued_ = false;
        // A BLE loss makes any unconfirmed effect command unknowable. The
        // ControllerState has already made the rendered value stale; discard
        // action metadata as well so it cannot be mistaken for a later link.
        if (hasPendingFxOperation()) {
            cancelFxRequest(state_, nullptr, false);
        }
        return;
    }

    // The legacy startup flow fetches the complete current preset but not its
    // hardware-preset number. The controller cannot safely enable a preset
    // action until that separate Spark-owned value has been observed.
    if (snapshot.connectionPhase == ControllerConnectionPhase::Syncing &&
        (!currentPresetQueryIssued_ || millis() - currentPresetQueryAtMs_ >= kPresetTimeoutMs)) {
        Serial.println("Controller: requesting current hardware preset");
        currentPresetQueryIssued_ = dataControl.getCurrentPresetNum();
        if (currentPresetQueryIssued_) {
            currentPresetQueryAtMs_ = millis();
        }
        return;
    }

    if (sentPreset_ != 0) {
        if (snapshot.connectionPhase != ControllerConnectionPhase::Ready) {
            state_.failHardwarePresetRequest();
            sentPreset_ = 0;
        } else if (!awaitingConfirmationQuery_ &&
                   SparkDataControl::finalAckRevision() != sentAfterAckRevision_) {
            const AckData ack = SparkDataControl::lastFinalAck();
            sentAfterAckRevision_ = SparkDataControl::finalAckRevision();
            if (ack.subcmd == 0x38) {
                // Spark NEO Core accepts a hardware-preset command without
                // necessarily broadcasting a new preset number. ACK is only
                // a transport milestone; request the authoritative value.
                Serial.println("Controller: preset ACK received; verifying Spark state");
                dataControl.getCurrentPresetNum();
                awaitingConfirmationQuery_ = true;
                sentAtMs_ = millis();
            }
        } else if (awaitingConfirmationQuery_ && snapshot.confirmedHardwarePreset == sentPreset_) {
            Serial.printf("Controller: preset %u confirmed by Spark\n", sentPreset_);
            state_.confirmHardwarePresetRequest();
            sentPreset_ = 0;
        } else if (awaitingConfirmationQuery_ && snapshot.confirmedHardwarePreset != 0 &&
                   snapshot.confirmedHardwarePreset != presetBeforeRequest_) {
            // A reported preset change other than our intended target wins.
            // It is an external/conflicting action, never a local success.
            state_.failHardwarePresetRequest();
            Serial.printf("Controller: preset conflict (Spark reports %u)\n", snapshot.confirmedHardwarePreset);
            sentPreset_ = 0;
        } else if (millis() - sentAtMs_ >= kPresetTimeoutMs) {
            Serial.println("Controller: preset confirmation timed out; resyncing");
            state_.failHardwarePresetRequest();
            dataControl.getCurrentPresetFromSpark();
            sentPreset_ = 0;
        }
        return;
    }

    if (sentFxSlot_ != kNoFxSlot) {
        if (snapshot.connectionPhase != ControllerConnectionPhase::Ready || snapshot.sparkStateStale ||
            sentFxSlot_ >= snapshot.fxSlots.size() ||
            snapshot.fxChainIdentity != fxChainIdentityBeforeRequest_ ||
            snapshot.confirmedHardwarePreset != fxHardwarePresetBeforeRequest_) {
            // A preset/chain or link transition makes the command target
            // ambiguous. Do not retarget it to whatever happens to be loaded.
            cancelFxRequest(state_, &dataControl, true);
            return;
        }

        const ControllerFxSlot &fx = snapshot.fxSlots[sentFxSlot_];
        if (!fx.known || fx.modelName != sentFxModelName_) {
            cancelFxRequest(state_, &dataControl, true);
            return;
        }

        // A final ACK only says the transport saw an acknowledgement. The
        // controller confirms solely when its exact Spark model is freshly
        // observed in the requested bypass/on-off state. The before-value
        // guard prevents pending metadata itself from being treated as proof.
        const bool modelObservedAfterSend =
            SparkDataControl::fxModelObservationRevision(sentFxModelName_) != fxModelObservationRevisionBeforeRequest_;
        if (modelObservedAfterSend && fx.enabled == sentFxDesiredEnabled_ &&
            fx.enabled != fxEnabledBeforeRequest_) {
            Serial.printf("Controller: FX %u (%s) confirmed by Spark\n", sentFxSlot_, sentFxModelName_.c_str());
            state_.confirmFxToggleRequest(sentFxSlot_);
            clearFxRequest();
        } else if (millis() - fxSentAtMs_ >= kFxTimeoutMs) {
            Serial.printf("Controller: FX %u confirmation timed out; resyncing\n", sentFxSlot_);
            cancelFxRequest(state_, &dataControl, true);
        }
        return;
    }

    if (queuedPreset_ == 0) {
        // Preset and FX operations are serialized. An effect request captures
        // the exact Spark model/chain at tap time and cannot be retargeted.
        if (queuedFxSlot_ == kNoFxSlot) {
            return;
        }

        if (snapshot.connectionPhase != ControllerConnectionPhase::Ready || snapshot.sparkStateStale ||
            queuedFxSlot_ >= snapshot.fxSlots.size() ||
            snapshot.fxSlots[queuedFxSlot_].modelName != queuedFxModelName_ ||
            snapshot.fxChainIdentity != fxChainIdentityBeforeRequest_ ||
            snapshot.confirmedHardwarePreset != fxHardwarePresetBeforeRequest_) {
            cancelFxRequest(state_, &dataControl, true);
            return;
        }

        // Capture the protocol-derived generation before sending. Controller
        // pending/snapshot revisions are deliberately excluded: only a fresh
        // incoming FX_ONOFF update for this exact model may confirm success.
        const uint32_t observationBeforeSend =
            SparkDataControl::fxModelObservationRevision(queuedFxModelName_);
        if (SparkDataControl::switchEffectOnOff(queuedFxModelName_, queuedFxDesiredEnabled_)) {
            sentFxSlot_ = queuedFxSlot_;
            sentFxDesiredEnabled_ = queuedFxDesiredEnabled_;
            sentFxModelName_ = queuedFxModelName_;
            queuedFxSlot_ = kNoFxSlot;
            queuedFxModelName_.clear();
            fxSentAtMs_ = millis();
            fxModelObservationRevisionBeforeRequest_ = observationBeforeSend;
            Serial.printf("Controller: sending FX %u (%s) %s\n", sentFxSlot_, sentFxModelName_.c_str(),
                          sentFxDesiredEnabled_ ? "on" : "off");
        } else {
            cancelFxRequest(state_, &dataControl, true);
        }
        return;
    }
    const uint8_t preset = queuedPreset_;
    queuedPreset_ = 0;
    if (dataControl.changeHWPreset(preset)) {
        Serial.printf("Controller: sending preset %u\n", preset);
        sentPreset_ = preset;
        sentAtMs_ = millis();
        sentAfterAckRevision_ = SparkDataControl::finalAckRevision();
        awaitingConfirmationQuery_ = false;
    } else {
        state_.failHardwarePresetRequest();
    }
}
