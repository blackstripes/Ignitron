#include "controller/ControllerActions.h"

#include "controller/ControllerState.h"
#include "SparkDataControl.h"
#include "SparkPresetControl.h"
#include "SparkStatus.h"
#include "PersistentEventLog.h"

bool ControllerActions::requestHardwarePreset(uint8_t preset) {
    const ControllerSnapshot &snapshot = state_.snapshot();
    const uint8_t maxHardwarePreset =
        static_cast<uint8_t>(SparkPresetControl::getInstance().numberOfHWBanks() * PRESETS_PER_BANK);
    if (preset < 1 || preset > maxHardwarePreset || snapshot.connectionPhase != ControllerConnectionPhase::Ready ||
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

bool ControllerActions::requestTuner(bool on) {
    const ControllerSnapshot &snapshot = state_.snapshot();
    if (snapshot.tunerActive == on || snapshot.connectionPhase != ControllerConnectionPhase::Ready ||
        snapshot.sparkStateStale || snapshot.pendingHardwarePreset != 0 || sentPreset_ != 0 ||
        queuedPreset_ != 0 || hasPendingFxOperation() || queuedTunerRequest_ || tunerRequestSent_) {
        return false;
    }
    queuedTunerRequest_ = true;
    queuedTunerEnabled_ = on;
    return true;
}

void ControllerActions::cancelTunerEntry() {
    if (queuedTunerRequest_ && queuedTunerEnabled_) {
        queuedTunerRequest_ = false;
        return;
    }
    if (tunerRequestSent_ && tunerRequestEnabled_) tunerEntryCancelRequested_ = true;
}

bool ControllerActions::canRequestLooper() const {
    const ControllerSnapshot &snapshot = state_.snapshot();
    return snapshot.connectionPhase == ControllerConnectionPhase::Ready && !snapshot.sparkStateStale &&
           snapshot.looperCapability == ControllerLooperCapability::Verified && !snapshot.looperStale &&
           !snapshot.looperPending && snapshot.pendingHardwarePreset == 0 && sentPreset_ == 0 && queuedPreset_ == 0 &&
           !hasPendingFxOperation() && !queuedTunerRequest_ && !tunerRequestSent_ &&
           queuedLooperAction_ == LooperAction::None && sentLooperAction_ == LooperAction::None;
}

bool ControllerActions::queueLooperAction(LooperAction action) {
    if (!canRequestLooper()) return false;
    state_.disarmLooperClear();
    queuedLooperAction_ = action;
    state_.beginLooperRequest();
    return true;
}

bool ControllerActions::requestLooperRecordDub() { return queueLooperAction(LooperAction::RecordDub); }
bool ControllerActions::requestLooperPlayStop() {
    return state_.snapshot().looperLoopCount > 0 && queueLooperAction(LooperAction::PlayStop);
}
bool ControllerActions::requestLooperPlay() {
    return state_.snapshot().looperLoopCount > 0 && queueLooperAction(LooperAction::Play);
}
bool ControllerActions::requestLooperStop() {
    return state_.snapshot().looperLoopCount > 0 && queueLooperAction(LooperAction::Stop);
}
bool ControllerActions::requestLooperUndoRedo() {
    return state_.snapshot().looperLoopCount > 0 && queueLooperAction(LooperAction::UndoRedo);
}

bool ControllerActions::requestLooperClear() {
    const ControllerSnapshot &snapshot = state_.snapshot();
    if (snapshot.looperLoopCount == 0) return false;
    if (snapshot.looperClearArmed) {
        state_.disarmLooperClear();
        return queueLooperAction(LooperAction::Clear);
    }
    if (!canRequestLooper()) return false;
    state_.armLooperClear();
    looperSentAtMs_ = millis();
    return true;
}

void ControllerActions::cancelLooperClear() {
    state_.disarmLooperClear();
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
        SparkDataControl::recordControllerFxFailure();
        persistentEventLog.record(PersistentEvent::FxFailed, slot, true);
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
        currentPresetQueryAtMs_ = 0;
        startupFullPresetQueryIssued_ = false;
        startupFullPresetQueryAtMs_ = 0;
        startupFullPresetQueryMessageNumber_ = 0;
        queuedPreset_ = 0;
        if (sentPreset_ != 0) {
            state_.failHardwarePresetRequest();
            SparkDataControl::recordControllerPresetFailure();
            persistentEventLog.record(PersistentEvent::PresetFailed, sentPreset_, true);
            sentPreset_ = 0;
            awaitingConfirmationQuery_ = false;
            awaitingPresetFullResponse_ = false;
            presetFullObservationRevisionBeforeQuery_ = 0;
            presetFullQueryMessageNumber_ = 0;
        }
        queuedTunerRequest_ = false;
        tunerRequestSent_ = false;
        tunerEntryCancelRequested_ = false;
        queuedLooperAction_ = LooperAction::None;
        sentLooperAction_ = LooperAction::None;
        looperSyncRequestedAtMs_ = 0;
        // A BLE loss makes any unconfirmed effect command unknowable. The
        // ControllerState has already made the rendered value stale; discard
        // action metadata as well so it cannot be mistaken for a later link.
        if (hasPendingFxOperation()) {
            cancelFxRequest(state_, nullptr, false, "BLE disconnected");
        }
        return;
    }

    if (snapshot.looperClearArmed && queuedLooperAction_ == LooperAction::None &&
        millis() - looperSentAtMs_ >= kLooperClearArmMs) {
        state_.disarmLooperClear();
    }

    if (sentLooperAction_ != LooperAction::None) {
        const bool commandObserved = SparkDataControl::looperCommandObservationRevision() != looperCommandRevisionBeforeRequest_;
        const bool statusObserved = SparkDataControl::looperStatusObservationRevision() != looperStatusRevisionBeforeRequest_;
        const byte observedCommand = SparkStatus::getInstance().lastLooperCommand();
        bool confirmed = false;
        if (sentLooperAction_ == LooperAction::Clear) {
            confirmed = statusObserved && snapshot.looperLoopCount == 0;
        } else if (sentLooperAction_ == LooperAction::UndoRedo) {
            confirmed = commandObserved && (observedCommand == SPK_LOOPER_CMD_UNDO || observedCommand == SPK_LOOPER_CMD_REDO);
        } else if (sentLooperAction_ == LooperAction::PlayStop) {
            confirmed = commandObserved && (snapshot.looperTransport == ControllerLooperTransport::Playing ||
                                             snapshot.looperTransport == ControllerLooperTransport::Stopped);
        } else if (sentLooperAction_ == LooperAction::Play) {
            confirmed = commandObserved && observedCommand == SPK_LOOPER_CMD_PLAY;
        } else if (sentLooperAction_ == LooperAction::Stop) {
            confirmed = commandObserved && observedCommand == SPK_LOOPER_CMD_STOP;
        } else if (sentLooperAction_ == LooperAction::RecordDub) {
            confirmed = commandObserved && (snapshot.looperTransport == ControllerLooperTransport::Recording ||
                                             snapshot.looperTransport == ControllerLooperTransport::Overdubbing ||
                                             snapshot.looperTransport == ControllerLooperTransport::Playing);
        }
        // Only a fresh, action-appropriate Spark notification/status update
        // resolves pending; command transport ACKs never enter this path.
        if (confirmed) {
            state_.confirmLooperRequest();
            sentLooperAction_ = LooperAction::None;
        } else if (millis() - looperSentAtMs_ >= kLooperTimeoutMs) {
            state_.failLooperRequest();
            dataControl.sparkLooperGetStatus();
            dataControl.sparkLooperGetConfig();
            sentLooperAction_ = LooperAction::None;
        }
        return;
    }

    if (queuedLooperAction_ != LooperAction::None) {
        const LooperAction action = queuedLooperAction_;
        queuedLooperAction_ = LooperAction::None;
        const uint32_t commandRevisionBeforeSend = SparkDataControl::looperCommandObservationRevision();
        const uint32_t statusRevisionBeforeSend = SparkDataControl::looperStatusObservationRevision();
        bool sent = false;
        switch (action) {
        case LooperAction::RecordDub:
            // Choose the native sequence from observed transport only; this
            // does not alter ControllerState until Spark notifies us back.
            if (snapshot.looperTransport == ControllerLooperTransport::Recording ||
                snapshot.looperTransport == ControllerLooperTransport::Overdubbing) sent = dataControl.sparkLooperStopRecAndPlay();
            else if (snapshot.looperLoopCount > 0) sent = dataControl.sparkLooperDub();
            else sent = dataControl.sparkLooperRec();
            break;
        case LooperAction::PlayStop:
            sent = (snapshot.looperTransport == ControllerLooperTransport::Playing ||
                    snapshot.looperTransport == ControllerLooperTransport::Overdubbing)
                       ? dataControl.sparkLooperStopPlaying() : dataControl.sparkLooperPlay();
            break;
        case LooperAction::UndoRedo: sent = dataControl.sparkLooperUndoRedo(); break;
        case LooperAction::Play: sent = dataControl.sparkLooperPlay(); break;
        case LooperAction::Stop: sent = dataControl.sparkLooperStopPlaying(); break;
        case LooperAction::Clear: sent = dataControl.sparkLooperDeleteAll(); break;
        default: break;
        }
        if (sent) {
            sentLooperAction_ = action;
            looperSentAtMs_ = millis();
            looperCommandRevisionBeforeRequest_ = commandRevisionBeforeSend;
            looperStatusRevisionBeforeRequest_ = statusRevisionBeforeSend;
        }
        else state_.failLooperRequest();
        return;
    }

    // Do not locally manufacture tuner state. Spark TUNER_ON/OFF observations
    // are the only confirmation that moves the controller in or out of tuner.
    if (tunerRequestSent_) {
        if (snapshot.tunerActive == tunerRequestEnabled_) {
            if (tunerRequestEnabled_ && tunerEntryCancelRequested_) {
                // TUNER_ON arrived after a newer navigation destination.
                tunerRequestSent_ = false;
                tunerEntryCancelRequested_ = false;
                queuedTunerRequest_ = true;
                queuedTunerEnabled_ = false;
                return;
            }
            Serial.printf("Controller: tuner %s confirmed by Spark\n",
                          tunerRequestEnabled_ ? "entry" : "exit");
            tunerRequestSent_ = false;
            tunerEntryCancelRequested_ = false;
        } else if (millis() - tunerRequestSentAtMs_ >= kTunerTimeoutMs) {
            Serial.printf("Controller: tuner %s timed out\n",
                          tunerRequestEnabled_ ? "entry" : "exit");
            tunerRequestSent_ = false;
            tunerEntryCancelRequested_ = false;
        }
        return;
    }

    if (queuedTunerRequest_) {
        queuedTunerRequest_ = false;
        if (!queuedTunerEnabled_) {
            // Spark 2 accepted the native 0x01/0x65-off command in testing
            // but did not consistently emit TUNER_OFF. The project's normal
            // tuner-off path makes the same request and restores preset mode;
            // a later fresh tuner output still wins and re-enters tuner.
            SparkDataControl::switchSubMode(SUB_MODE_PRESET);
            Serial.println("Controller: requested tuner exit via preset mode");
            return;
        }
        if (SparkDataControl::switchTuner(queuedTunerEnabled_)) {
            tunerRequestSent_ = true;
            tunerRequestEnabled_ = queuedTunerEnabled_;
            tunerRequestSentAtMs_ = millis();
            Serial.printf("Controller: requesting tuner %s\n",
                          queuedTunerEnabled_ ? "entry" : "exit");
        } else {
            Serial.printf("Controller: tuner %s command failed\n",
                          queuedTunerEnabled_ ? "entry" : "exit");
        }
        return;
    }

    // The verified capability is not itself a fresh looper state. Request
    // both authoritative config and status once the normal preset readiness
    // barrier has completed; retry boundedly while the amp remains silent.
    if (snapshot.connectionPhase == ControllerConnectionPhase::Ready &&
        snapshot.looperCapability == ControllerLooperCapability::Verified &&
        (!snapshot.looperKnown || !snapshot.looperSettingsKnown) &&
        (looperSyncRequestedAtMs_ == 0 || millis() - looperSyncRequestedAtMs_ >= kLooperTimeoutMs)) {
        dataControl.sparkLooperGetConfig();
        dataControl.sparkLooperGetStatus();
        looperSyncRequestedAtMs_ = millis();
        return;
    }

    // The legacy startup flow can fetch a complete current preset but not its
    // hardware-preset number. The controller cannot safely enable a preset
    // action until that separate Spark-owned value has been observed.
    if (!hasPendingFxOperation() && snapshot.connectionPhase == ControllerConnectionPhase::Syncing &&
        snapshot.confirmedHardwarePreset == 0 &&
        (!currentPresetQueryIssued_ || millis() - currentPresetQueryAtMs_ >= kPresetTimeoutMs)) {
        Serial.println("Controller: requesting current hardware preset");
        currentPresetQueryIssued_ = dataControl.getCurrentPresetNum();
        if (currentPresetQueryIssued_) {
            currentPresetQueryAtMs_ = millis();
        }
        return;
    }

    if (!hasPendingFxOperation() && snapshot.connectionPhase == ControllerConnectionPhase::Syncing &&
        snapshot.confirmedHardwarePreset != 0 && !snapshot.fullPresetObservedForLink) {
        // PanelLan owns the startup full-preset request, avoiding races with
        // legacy cache restoration and giving the current link one authority.
        if (!startupFullPresetQueryIssued_ ||
            millis() - startupFullPresetQueryAtMs_ >= kPresetTimeoutMs) {
            startupFullPresetQueryIssued_ = dataControl.getCurrentPresetFromSpark(&startupFullPresetQueryMessageNumber_);
            startupFullPresetQueryAtMs_ = millis();
            if (startupFullPresetQueryIssued_) {
                state_.expectStartupFullPreset(startupFullPresetQueryMessageNumber_);
            }
            Serial.printf("Controller: requesting startup full preset (%s)\n",
                          startupFullPresetQueryIssued_ ? "sent" : "send failed");
        }
        return;
    }

    if (sentPreset_ != 0) {
        if (snapshot.connectionPhase != ControllerConnectionPhase::Ready) {
            state_.failHardwarePresetRequest();
            SparkDataControl::recordControllerPresetFailure();
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
        } else if (awaitingConfirmationQuery_ && !awaitingPresetFullResponse_ &&
                   snapshot.confirmedHardwarePreset == sentPreset_) {
            // The preset number can arrive before its full Spark-owned preset.
            // Keep FX unavailable until that authoritative response replaces
            // any cached model chain.
            presetFullObservationRevisionBeforeQuery_ = SparkDataControl::fullPresetObservationRevision();
            awaitingPresetFullResponse_ = dataControl.getCurrentPresetFromSpark(&presetFullQueryMessageNumber_);
            if (awaitingPresetFullResponse_) {
                sentAtMs_ = millis();
                Serial.printf("Controller: preset %u number confirmed; syncing full preset\n", sentPreset_);
            } else {
                Serial.println("Controller: full preset sync request failed");
                state_.failHardwarePresetRequest();
                SparkDataControl::recordControllerPresetFailure();
                sentPreset_ = 0;
            }
        } else if (awaitingPresetFullResponse_ && snapshot.confirmedHardwarePreset != 0 &&
                   snapshot.confirmedHardwarePreset != sentPreset_) {
            // A physical/external preset change remains a conflict while the
            // full-preset verification query is in flight.
            state_.failHardwarePresetRequest();
            SparkDataControl::recordControllerPresetFailure();
            Serial.printf("Controller: preset conflict (Spark reports %u)\n", snapshot.confirmedHardwarePreset);
            sentPreset_ = 0;
            awaitingPresetFullResponse_ = false;
        } else if (awaitingPresetFullResponse_ &&
                   SparkDataControl::fullPresetObservationRevision() != presetFullObservationRevisionBeforeQuery_ &&
                   SparkDataControl::fullPresetObservationMessageNumber() == presetFullQueryMessageNumber_) {
            Serial.printf("Controller: preset %u confirmed by Spark\n", sentPreset_);
            state_.confirmHardwarePresetRequest();
            SparkDataControl::recordControllerPresetConfirm();
            persistentEventLog.record(PersistentEvent::PresetConfirmed, sentPreset_, true);
            sentPreset_ = 0;
            awaitingPresetFullResponse_ = false;
        } else if (awaitingConfirmationQuery_ && !awaitingPresetFullResponse_ && snapshot.confirmedHardwarePreset != 0 &&
                   snapshot.confirmedHardwarePreset != presetBeforeRequest_) {
            // A reported preset change other than our intended target wins.
            // It is an external/conflicting action, never a local success.
            state_.failHardwarePresetRequest();
            SparkDataControl::recordControllerPresetFailure();
            Serial.printf("Controller: preset conflict (Spark reports %u)\n", snapshot.confirmedHardwarePreset);
            sentPreset_ = 0;
        } else if (millis() - sentAtMs_ >= kPresetTimeoutMs) {
            Serial.println("Controller: preset confirmation timed out; resyncing");
            state_.failHardwarePresetRequest();
            SparkDataControl::recordControllerPresetFailure();
            dataControl.getCurrentPresetFromSpark();
            sentPreset_ = 0;
            awaitingPresetFullResponse_ = false;
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
            SparkDataControl::recordControllerFxConfirm();
            persistentEventLog.record(PersistentEvent::FxConfirmed, sentFxSlot_, true);
            clearFxRequest();
        } else if (modelObservedAfterSend && fx.known && fx.modelName == sentFxModelName_ &&
                   fx.enabled != sentFxDesiredEnabled_) {
            cancelFxRequest(state_, &dataControl, true, "FX_ONOFF reported conflicting state");
        } else if (fullPresetObservedAfterSend && fx.known && fx.modelName == sentFxModelName_ &&
                   fx.enabled == sentFxDesiredEnabled_ && fx.enabled != fxEnabledBeforeRequest_) {
            Serial.printf("Controller: FX %u (%s) confirmed by full preset response\n", sentFxSlot_, sentFxModelName_.c_str());
            state_.confirmFxToggleRequest(sentFxSlot_);
            SparkDataControl::recordControllerFxConfirm();
            persistentEventLog.record(PersistentEvent::FxConfirmed, sentFxSlot_, true);
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
                if (fxFullPresetQueryIssued_) {
                    // The full-preset query is a second protocol round trip.
                    // Its confirmation window starts when that query is sent,
                    // rather than when the original FX command was sent.
                    fxSentAtMs_ = millis();
                }
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
            SparkDataControl::recordControllerFxSend();
            persistentEventLog.record(PersistentEvent::FxSend, sentFxSlot_);
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
        awaitingPresetFullResponse_ = false;
        SparkDataControl::recordControllerPresetSend();
        persistentEventLog.record(PersistentEvent::PresetSend, preset);
    } else {
        state_.failHardwarePresetRequest();
        SparkDataControl::recordControllerPresetFailure();
        persistentEventLog.record(PersistentEvent::PresetFailed, preset, true);
    }
}
