#include "controller/ControllerActions.h"

#include "controller/ControllerState.h"
#include "SparkDataControl.h"

bool ControllerActions::requestHardwarePreset(uint8_t preset) {
    const ControllerSnapshot &snapshot = state_.snapshot();
    if (preset < 1 || preset > 4 || snapshot.connectionPhase != ControllerConnectionPhase::Ready ||
        snapshot.sparkStateStale || snapshot.pendingHardwarePreset != 0 || hasPendingFxOperation() ||
        queuedTunerRequest_ || tunerRequestSent_ ||
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
        queuedPreset_ != 0 || hasPendingFxOperation() || queuedTunerRequest_ || tunerRequestSent_) {
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
    fxChainIdentityBeforeRequest_ = snapshot.fxChainIdentity;
    fxFullPresetObservationRevisionBeforeRequest_ = SparkDataControl::fullPresetObservationRevision();
    state_.beginFxToggleRequest(slot, queuedFxDesiredEnabled_);
    return true;
}

bool ControllerActions::requestTuner() {
    const ControllerSnapshot &snapshot = state_.snapshot();
    if (snapshot.tunerActive || snapshot.connectionPhase != ControllerConnectionPhase::Ready ||
        snapshot.sparkStateStale || snapshot.pendingHardwarePreset != 0 || sentPreset_ != 0 ||
        queuedPreset_ != 0 || hasPendingFxOperation() || queuedTunerRequest_ || tunerRequestSent_) {
        return false;
    }
    queuedTunerRequest_ = true;
    return true;
}

bool ControllerActions::hasPendingFxOperation() const {
    return queuedFxSlot_ != kNoFxSlot || sentFxSlot_ != kNoFxSlot;
}

void ControllerActions::cancelFxRequest(ControllerState &state, SparkDataControl *dataControl, bool refresh,
                                        const char *reason) {
    const uint8_t slot = sentFxSlot_ != kNoFxSlot ? sentFxSlot_ : queuedFxSlot_;
    if (slot != kNoFxSlot) {
        Serial.printf("Controller: FX %u cancelled: %s\n", slot, reason);
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
    fxFullPresetObservationRevisionBeforeRequest_ = 0;
    fxSentAfterAckRevision_ = 0;
    sentFxMessageNumber_ = 0;
    fxFullPresetQueryIssued_ = false;
}

void ControllerActions::process(SparkDataControl &dataControl) {
    const ControllerSnapshot &snapshot = state_.snapshot();
    if (!SparkDataControl::isAmpConnected()) {
        currentPresetQueryIssued_ = false;
        queuedTunerRequest_ = false;
        tunerRequestSent_ = false;
        // A BLE loss makes any unconfirmed effect command unknowable. The
        // ControllerState has already made the rendered value stale; discard
        // action metadata as well so it cannot be mistaken for a later link.
        if (hasPendingFxOperation()) {
            cancelFxRequest(state_, nullptr, false, "BLE disconnected");
        }
        return;
    }

    // Do not locally manufacture tuner state. A Spark TUNER_ON observation
    // is the only confirmation that moves the controller into tuner mode.
    if (tunerRequestSent_) {
        if (snapshot.tunerActive) {
            Serial.println("Controller: tuner entry confirmed by Spark");
            tunerRequestSent_ = false;
        } else if (millis() - tunerRequestSentAtMs_ >= kTunerTimeoutMs) {
            Serial.println("Controller: tuner entry timed out");
            tunerRequestSent_ = false;
        }
        return;
    }

    if (queuedTunerRequest_) {
        queuedTunerRequest_ = false;
        if (SparkDataControl::switchTuner(true)) {
            tunerRequestSent_ = true;
            tunerRequestSentAtMs_ = millis();
            Serial.println("Controller: requesting tuner entry");
        } else {
            Serial.println("Controller: tuner entry command failed");
        }
        return;
    }

    // The legacy startup flow fetches the complete current preset but not its
    // hardware-preset number. The controller cannot safely enable a preset
    // action until that separate Spark-owned value has been observed.
    if (!hasPendingFxOperation() && snapshot.connectionPhase == ControllerConnectionPhase::Syncing &&
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
        if (sentFxSlot_ >= snapshot.fxSlots.size()) {
            cancelFxRequest(state_, &dataControl, true, "invalid FX slot");
            return;
        }

        const ControllerFxSlot &fx = snapshot.fxSlots[sentFxSlot_];
        const bool fullPresetObservedAfterSend =
            SparkDataControl::fullPresetObservationRevision() != fxFullPresetObservationRevisionBeforeRequest_;
        if (fullPresetObservedAfterSend &&
            (!fx.known || fx.modelName != sentFxModelName_ || snapshot.fxChainIdentity != fxChainIdentityBeforeRequest_)) {
            // Only an applied full-preset response can establish a target
            // chain change. NEO's transient unknown hardware-preset number
            // and Syncing phase are not such a change.
            cancelFxRequest(state_, &dataControl, true, "Spark full preset changed target model/chain");
            return;
        }

        // A direct FX_ONOFF update is the preferred confirmation path.
        const bool modelObservedAfterSend =
            SparkDataControl::fxModelObservationRevision(sentFxModelName_) != fxModelObservationRevisionBeforeRequest_;
        if (modelObservedAfterSend && fx.enabled == sentFxDesiredEnabled_ &&
            fx.enabled != fxEnabledBeforeRequest_) {
            Serial.printf("Controller: FX %u (%s) confirmed by FX_ONOFF\n", sentFxSlot_, sentFxModelName_.c_str());
            state_.confirmFxToggleRequest(sentFxSlot_);
            clearFxRequest();
        } else if (modelObservedAfterSend && fx.known && fx.modelName == sentFxModelName_ &&
                   fx.enabled != sentFxDesiredEnabled_) {
            cancelFxRequest(state_, &dataControl, true, "FX_ONOFF reported conflicting state");
        } else if (fullPresetObservedAfterSend && fx.known && fx.modelName == sentFxModelName_ &&
                   fx.enabled == sentFxDesiredEnabled_ && fx.enabled != fxEnabledBeforeRequest_) {
            Serial.printf("Controller: FX %u (%s) confirmed by full preset response\n", sentFxSlot_, sentFxModelName_.c_str());
            state_.confirmFxToggleRequest(sentFxSlot_);
            clearFxRequest();
        } else if (fullPresetObservedAfterSend && fx.known && fx.modelName == sentFxModelName_ &&
                   fx.enabled != sentFxDesiredEnabled_) {
            cancelFxRequest(state_, &dataControl, true, "full preset reported conflicting FX state");
        } else if (!fxFullPresetQueryIssued_ &&
                   SparkDataControl::finalAckRevision() != fxSentAfterAckRevision_) {
            const AckData ack = SparkDataControl::lastFinalAck();
            fxSentAfterAckRevision_ = SparkDataControl::finalAckRevision();
            if (ack.subcmd == 0x15 && ack.msgNum == sentFxMessageNumber_) {
                // NEO Core can ACK an effect change without a separate
                // FX_ONOFF event. The ACK starts a query; it never confirms.
                fxFullPresetQueryIssued_ = dataControl.getCurrentPresetFromSpark();
                Serial.printf("Controller: FX %u ACK received; querying full preset (%s)\n", sentFxSlot_,
                              fxFullPresetQueryIssued_ ? "sent" : "send failed");
            }
        } else if (millis() - fxSentAtMs_ >= kFxTimeoutMs) {
            cancelFxRequest(state_, &dataControl, true, "confirmation timed out");
        }
        return;
    }

    if (queuedPreset_ == 0) {
        // Preset and FX operations are serialized. An effect request captures
        // the exact Spark model/chain at tap time and cannot be retargeted.
        if (queuedFxSlot_ == kNoFxSlot) {
            return;
        }

        if (queuedFxSlot_ >= snapshot.fxSlots.size()) {
            cancelFxRequest(state_, &dataControl, true, "invalid queued FX slot");
            return;
        }
        const bool fullPresetObservedBeforeSend =
            SparkDataControl::fullPresetObservationRevision() != fxFullPresetObservationRevisionBeforeRequest_;
        const ControllerFxSlot &queuedFx = snapshot.fxSlots[queuedFxSlot_];
        if (fullPresetObservedBeforeSend &&
            (!queuedFx.known || queuedFx.modelName != queuedFxModelName_ ||
             snapshot.fxChainIdentity != fxChainIdentityBeforeRequest_)) {
            cancelFxRequest(state_, &dataControl, true, "Spark full preset changed queued target model/chain");
            return;
        }

        // Capture the protocol-derived generation before sending. Controller
        // pending/snapshot revisions are deliberately excluded: only a fresh
        // incoming FX_ONOFF update for this exact model may confirm success.
        const uint32_t observationBeforeSend =
            SparkDataControl::fxModelObservationRevision(queuedFxModelName_);
        uint8_t messageNumber = 0;
        if (SparkDataControl::switchEffectOnOff(queuedFxModelName_, queuedFxDesiredEnabled_, &messageNumber)) {
            sentFxSlot_ = queuedFxSlot_;
            sentFxDesiredEnabled_ = queuedFxDesiredEnabled_;
            sentFxModelName_ = queuedFxModelName_;
            queuedFxSlot_ = kNoFxSlot;
            queuedFxModelName_.clear();
            fxSentAtMs_ = millis();
            fxModelObservationRevisionBeforeRequest_ = observationBeforeSend;
            fxFullPresetObservationRevisionBeforeRequest_ = SparkDataControl::fullPresetObservationRevision();
            fxSentAfterAckRevision_ = SparkDataControl::finalAckRevision();
            sentFxMessageNumber_ = messageNumber;
            fxFullPresetQueryIssued_ = false;
            Serial.printf("Controller: sending FX %u (%s) %s\n", sentFxSlot_, sentFxModelName_.c_str(),
                          sentFxDesiredEnabled_ ? "on" : "off");
        } else {
            cancelFxRequest(state_, &dataControl, true, "Spark command send failed");
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
