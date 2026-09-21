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
        queuedTunerRequest_ || tunerRequestSent_ || queuedLooperAction_ != LooperAction::None ||
        sentLooperAction_ != LooperAction::None ||
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
        queuedPreset_ != 0 || hasPendingFxOperation() || queuedTunerRequest_ || tunerRequestSent_ ||
        queuedLooperAction_ != LooperAction::None || sentLooperAction_ != LooperAction::None) {
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
    if (!on) {
        // Native OFF is safe and idempotent. Prefer restoring audible output
        // over preserving a stale local operation; it may supersede an entry.
        if (queuedTunerRequest_ && queuedTunerEnabled_) queuedTunerRequest_ = false;
        if (tunerRequestSent_ && tunerRequestEnabled_) {
            tunerRequestSent_ = false;
            tunerEntryCancelRequested_ = false;
        }
        tunerExitIntent_ = true;
        queuedTunerRequest_ = true;
        queuedTunerEnabled_ = false;
        return true;
    }
    // An explicit return to tuner cancels an OFF that has not reached the
    // transport yet. Once its first packet was accepted it cannot be recalled;
    // retain the exit intent until that command has settled.
    if (tunerExitIntent_) {
        if (queuedTunerRequest_ && !queuedTunerEnabled_) {
            queuedTunerRequest_ = false;
            tunerExitIntent_ = false;
        } else if (tunerRequestSent_ && !tunerRequestEnabled_) {
            return false;
        } else {
            tunerExitIntent_ = false;
        }
    }
    if (snapshot.connectionPhase != ControllerConnectionPhase::Ready || snapshot.sparkStateStale ||
        snapshot.pendingHardwarePreset != 0 || sentPreset_ != 0 || queuedPreset_ != 0 ||
        hasPendingFxOperation() || queuedLooperAction_ != LooperAction::None ||
        sentLooperAction_ != LooperAction::None) {
        return false;
    }
    if (snapshot.tunerActive || queuedTunerRequest_ || tunerRequestSent_) return false;
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

void ControllerActions::printState(Stream &out) const {
    const ControllerSnapshot &snapshot = state_.snapshot();
    out.printf("Controller: rev=%lu phase=%u preset=%u pending=%u tuner=%u looper=%u transport=%u loops=%u pending=%u failed=%u\n",
               static_cast<unsigned long>(snapshot.revision), static_cast<unsigned>(snapshot.connectionPhase),
               snapshot.confirmedHardwarePreset, snapshot.pendingHardwarePreset, snapshot.tunerActive ? 1 : 0,
               static_cast<unsigned>(snapshot.looperCapability), static_cast<unsigned>(snapshot.looperTransport),
               snapshot.looperLoopCount, snapshot.looperPending ? 1 : 0, snapshot.looperActionFailed ? 1 : 0);
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
    cancelDeferredLooperRecord();
    return state_.snapshot().looperLoopCount > 0 && queueLooperAction(LooperAction::PlayStop);
}
bool ControllerActions::requestLooperPlay() {
    cancelDeferredLooperRecord();
    return state_.snapshot().looperLoopCount > 0 && queueLooperAction(LooperAction::Play);
}
bool ControllerActions::requestLooperStop() {
    cancelDeferredLooperRecord();
    return state_.snapshot().looperLoopCount > 0 && queueLooperAction(LooperAction::Stop);
}
bool ControllerActions::requestLooperUndoRedo() {
    cancelDeferredLooperRecord();
    return state_.snapshot().looperLoopCount > 0 && queueLooperAction(LooperAction::UndoRedo);
}

bool ControllerActions::requestLooperClear() {
    cancelDeferredLooperRecord();
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

void ControllerActions::cancelDeferredLooperRecord() {
    SparkDataControl::cancelPendingLooperRecord();
}

void ControllerActions::failLooperRequest(SparkDataControl *dataControl, const char *reason) {
    cancelDeferredLooperRecord();
    state_.failLooperRequest();
    persistentEventLog.record(PersistentEvent::LooperFailed, static_cast<uint16_t>(sentLooperAction_), true);
    Serial.printf("Controller: looper action failed: %s\n", reason);
    // The command-boundary FIFO retains this recovery pair behind any active
    // multipart tail. Config precedes status so the status is interpreted with
    // the refreshed looper settings.
    if (dataControl != nullptr) {
        dataControl->sparkLooperGetConfig();
        dataControl->sparkLooperGetStatus();
    }
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
        ampIdentityQueryAtMs_ = 0;
        ampIdentityWaitStarted_ = false;
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
        tunerExitIntent_ = false;
        queuedLooperAction_ = LooperAction::None;
        if (sentLooperAction_ != LooperAction::None) failLooperRequest(nullptr, "BLE disconnected");
        sentLooperAction_ = LooperAction::None;
        cancelDeferredLooperRecord();
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

    // BLE subscription may complete before Spark is ready to answer the
    // initial identity query. Retry boundedly while Identifying rather than
    // leaving the controller unusable after one lost response.
    if (snapshot.connectionPhase == ControllerConnectionPhase::Identifying && !ampIdentityWaitStarted_) {
        // checkBLEConnection() already sent the first query immediately after
        // notification subscription. Give it a full response window first.
        ampIdentityWaitStarted_ = true;
        ampIdentityQueryAtMs_ = millis();
        return;
    }
    if (snapshot.connectionPhase == ControllerConnectionPhase::Identifying &&
        millis() - ampIdentityQueryAtMs_ >= kPresetTimeoutMs) {
        if (!SparkDataControl::hasPendingCommandPackets() && dataControl.getAmpName()) {
            ampIdentityQueryAtMs_ = millis();
            Serial.println("Controller: retrying amp identity query");
        }
        return;
    }

    // Tuner exit is a safety action, not a normal scheduler participant. Do
    // not overwrite the remaining packets of an in-flight command: wait for
    // its intermediate-ACK tail, then issue OFF before dispatching any other
    // queued controller action in this pass.
    if (tunerExitIntent_) {
        if (tunerRequestSent_ && !tunerRequestEnabled_) {
            if (SparkDataControl::tunerOffObservationRevision() != tunerOffObservationRevisionBeforeRequest_) {
                Serial.println("Controller: tuner exit confirmed by Spark");
                tunerRequestSent_ = false;
                persistentEventLog.record(PersistentEvent::TunerConfirmed, 0, true);
            } else if (snapshot.tunerActive) {
                // Local presentation was released when OFF was accepted, so
                // activity while waiting is a fresh Spark reassertion.
                // Reissue OFF immediately rather than consuming the grace
                // timeout while the amp may still be muted in tuner mode.
                Serial.println("Controller: tuner activity reasserted during OFF grace; retrying");
                tunerRequestSent_ = false;
                queuedTunerRequest_ = true;
                queuedTunerEnabled_ = false;
                persistentEventLog.record(PersistentEvent::TunerFailed, 0, true);
            } else if (millis() - tunerRequestSentAtMs_ >= kTunerTimeoutMs) {
                tunerRequestSent_ = false;
                // Spark 2 can accept OFF without reporting TUNER_OFF.  A fresh
                // TUNER_ON/output during this grace period is authoritative
                // evidence that it remained active, so try OFF again.  In the
                // absence of that reassertion, release only the local intent;
                // this is an outcome, never an observed Spark confirmation.
                tunerExitIntent_ = false;
                queuedTunerRequest_ = false;
                tunerEntryCancelRequested_ = false;
                persistentEventLog.record(PersistentEvent::TunerLocallyReleased, 0, true);
                Serial.println("Controller: tuner exit locally released after OFF grace timeout (no TUNER_OFF observed)");
            }
        }

        // A TUNER_ON/output arriving after a previously observed OFF is a
        // late reassertion, not permission for the tuner UI to take over.
        if (snapshot.tunerActive && !tunerRequestSent_) {
            queuedTunerRequest_ = true;
            queuedTunerEnabled_ = false;
        }

        if (queuedTunerRequest_) {
            if (SparkDataControl::hasPendingCommandPackets()) {
                return;
            }
            queuedTunerRequest_ = false;
            tunerOffObservationRevisionBeforeRequest_ = SparkDataControl::tunerOffObservationRevision();
            if (SparkDataControl::exitTuner()) {
                tunerRequestSent_ = true;
                tunerRequestEnabled_ = false;
                tunerRequestSentAtMs_ = millis();
                persistentEventLog.record(PersistentEvent::TunerSend, 0);
                // This only restores local presentation. Confirmation remains
                // exclusively the fresh TUNER_OFF observation above.
                SparkDataControl::switchSubMode(SUB_MODE_PRESET, false);
                Serial.println("Controller: native tuner exit locally released/sent; awaiting TUNER_OFF observation");
            } else {
                persistentEventLog.record(PersistentEvent::TunerFailed, 0, true);
                queuedTunerRequest_ = true;
                queuedTunerEnabled_ = false;
            }
            return;
        }
        if (tunerRequestSent_) {
            return;
        }
    }

    if (sentLooperAction_ != LooperAction::None) {
        if (SparkDataControl::looperDeferredRecordFailureRevision() !=
            looperDeferredRecordFailureRevisionBeforeRequest_) {
            failLooperRequest(&dataControl, "deferred REC command send failed");
            sentLooperAction_ = LooperAction::None;
            return;
        }
        const bool commandObserved = SparkDataControl::looperCommandObservationRevision() != looperCommandRevisionBeforeRequest_;
        const bool statusObserved = SparkDataControl::looperStatusObservationRevision() != looperStatusRevisionBeforeRequest_;
        const byte observedCommand = SparkStatus::getInstance().lastLooperCommand();
        const bool stopDeliveryAcknowledged =
            expectedLooperCommand_ == SPK_LOOPER_CMD_STOP && sentLooperMessageNumber_ != 0 &&
            !SparkDataControl::looperMessageNumberReused(sentLooperMessageNumber_) &&
            SparkDataControl::looperAckRevisionForMessage(sentLooperMessageNumber_) > looperAckRevisionBeforeRequest_;
        bool confirmed = false;
        if (sentLooperAction_ == LooperAction::Clear) {
            // A status-only empty response can belong to an external clear or
            // a delayed probe. Require this action's native DELETE observation.
            confirmed = commandObserved && observedCommand == SPK_LOOPER_CMD_DELETE &&
                        statusObserved && snapshot.looperLoopCount == 0;
        } else if (sentLooperAction_ == LooperAction::UndoRedo) {
            confirmed = commandObserved && (observedCommand == SPK_LOOPER_CMD_UNDO || observedCommand == SPK_LOOPER_CMD_REDO);
        } else if (sentLooperAction_ == LooperAction::PlayStop) {
            confirmed = commandObserved && observedCommand == expectedLooperCommand_;
        } else if (sentLooperAction_ == LooperAction::Play) {
            confirmed = commandObserved && observedCommand == expectedLooperCommand_;
        } else if (sentLooperAction_ == LooperAction::Stop) {
            confirmed = commandObserved && observedCommand == expectedLooperCommand_;
        } else if (sentLooperAction_ == LooperAction::RecordDub) {
            confirmed = commandObserved && observedCommand == expectedLooperCommand_;
        }
        // REC/DUB/CLEAR and every non-STOP operation remain strictly
        // observation-confirmed. Spark 2 can omit STOP's notification, so a
        // matching final delivery ACK is a bounded fallback only for STOP.
        if (confirmed) {
            state_.confirmLooperRequest();
            persistentEventLog.record(PersistentEvent::LooperConfirmed, static_cast<uint16_t>(sentLooperAction_), true);
            sentLooperAction_ = LooperAction::None;
            sentLooperMessageNumber_ = 0;
        } else if (stopDeliveryAcknowledged) {
            state_.acknowledgeLooperStopRequest();
            persistentEventLog.record(PersistentEvent::LooperAcknowledged,
                                      static_cast<uint16_t>(sentLooperAction_), true);
            sentLooperAction_ = LooperAction::None;
            sentLooperMessageNumber_ = 0;
        } else if (millis() - looperSentAtMs_ >= kLooperTimeoutMs) {
            failLooperRequest(&dataControl, "observation timed out");
            sentLooperAction_ = LooperAction::None;
            sentLooperMessageNumber_ = 0;
        }
        // A pending action owns this scheduler pass. In particular, do not
        // start a looper probe behind a just-issued STOP.
        return;
    }

    if (queuedLooperAction_ != LooperAction::None) {
        // Never overwrite an ACK-gated multipart tail. Retain the action
        // until every packet of the earlier command has drained.
        if (SparkDataControl::hasPendingCommandPackets()) return;
        const LooperAction action = queuedLooperAction_;
        queuedLooperAction_ = LooperAction::None;
        const uint32_t commandRevisionBeforeSend = SparkDataControl::looperCommandObservationRevision();
        const uint32_t statusRevisionBeforeSend = SparkDataControl::looperStatusObservationRevision();
        const uint32_t looperAckRevisionBeforeSend = SparkDataControl::looperAckRevision();
        uint8_t issuedLooperMessageNumber = 0;
        bool sent = false;
        switch (action) {
        case LooperAction::RecordDub:
            // Choose the native sequence from observed transport only; this
            // does not alter ControllerState until Spark notifies us back.
            if (snapshot.looperTransport == ControllerLooperTransport::Recording ||
                snapshot.looperTransport == ControllerLooperTransport::Overdubbing) {
                expectedLooperCommand_ = SPK_LOOPER_CMD_PLAY;
                sent = dataControl.sparkLooperStopRecAndPlay();
            } else if (snapshot.looperLoopCount > 0) {
                expectedLooperCommand_ = SPK_LOOPER_CMD_PLAY;
                sent = dataControl.sparkLooperDub();
            } else {
                expectedLooperCommand_ = SPK_LOOPER_CMD_REC;
                sent = dataControl.sparkLooperRec();
            }
            break;
        case LooperAction::PlayStop:
            expectedLooperCommand_ = (snapshot.looperTransport == ControllerLooperTransport::Playing ||
                                      snapshot.looperTransport == ControllerLooperTransport::Overdubbing)
                                         ? SPK_LOOPER_CMD_STOP : SPK_LOOPER_CMD_PLAY;
            sent = (snapshot.looperTransport == ControllerLooperTransport::Playing ||
                    snapshot.looperTransport == ControllerLooperTransport::Overdubbing)
                       ? dataControl.sparkLooperStopPlaying(&issuedLooperMessageNumber) : dataControl.sparkLooperPlay();
            break;
        case LooperAction::UndoRedo: sent = dataControl.sparkLooperUndoRedo(); break;
        case LooperAction::Play: expectedLooperCommand_ = SPK_LOOPER_CMD_PLAY; sent = dataControl.sparkLooperPlay(); break;
        case LooperAction::Stop: expectedLooperCommand_ = SPK_LOOPER_CMD_STOP; sent = dataControl.sparkLooperStopPlaying(&issuedLooperMessageNumber); break;
        case LooperAction::Clear: sent = dataControl.sparkLooperDeleteAll(); break;
        default: break;
        }
        if (sent) {
            sentLooperAction_ = action;
            looperSentAtMs_ = millis();
            looperCommandRevisionBeforeRequest_ = commandRevisionBeforeSend;
            looperStatusRevisionBeforeRequest_ = statusRevisionBeforeSend;
            // Use the global generation captured before the first write. The
            // per-message generation is compared against it above, avoiding a
            // stale ACK when the one-byte protocol sequence is reused.
            looperAckRevisionBeforeRequest_ = looperAckRevisionBeforeSend;
            looperDeferredRecordFailureRevisionBeforeRequest_ = SparkDataControl::looperDeferredRecordFailureRevision();
            sentLooperMessageNumber_ = issuedLooperMessageNumber;
            persistentEventLog.record(PersistentEvent::LooperSend, static_cast<uint16_t>(action));
        }
        else failLooperRequest(&dataControl, "command send failed");
        return;
    }

    // Do not locally manufacture tuner state. Spark TUNER_ON/OFF observations
    // are the only confirmation that moves the controller in or out of tuner.
    if (tunerRequestSent_) {
        const bool observedRequestedState = tunerRequestEnabled_
                                               ? snapshot.tunerActive
                                               : SparkDataControl::tunerOffObservationRevision() !=
                                                     tunerOffObservationRevisionBeforeRequest_;
        if (observedRequestedState) {
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
            persistentEventLog.record(PersistentEvent::TunerConfirmed, tunerRequestEnabled_ ? 1 : 0, true);
        } else if (millis() - tunerRequestSentAtMs_ >= kTunerTimeoutMs) {
            Serial.printf("Controller: tuner %s timed out\n",
                          tunerRequestEnabled_ ? "entry" : "exit");
            tunerRequestSent_ = false;
            tunerEntryCancelRequested_ = false;
            persistentEventLog.record(PersistentEvent::TunerFailed, tunerRequestEnabled_ ? 1 : 0, true);
        }
    }

    if (queuedTunerRequest_) {
        queuedTunerRequest_ = false;
        if (!queuedTunerEnabled_) {
            // Spark 2 accepted the native 0x01/0x65-off command in testing
            // but did not consistently emit TUNER_OFF. The project's normal
            // tuner-off path makes the same request and restores preset mode;
            // a later fresh tuner output still wins and re-enters tuner.
            tunerOffObservationRevisionBeforeRequest_ = SparkDataControl::tunerOffObservationRevision();
            if (SparkDataControl::exitTuner()) {
                tunerRequestSent_ = true;
                tunerRequestEnabled_ = false;
                tunerRequestSentAtMs_ = millis();
                persistentEventLog.record(PersistentEvent::TunerSend, 0);
                SparkDataControl::switchSubMode(SUB_MODE_PRESET, false);
                Serial.println("Controller: native tuner exit locally released/sent; awaiting TUNER_OFF observation");
            } else {
                persistentEventLog.record(PersistentEvent::TunerFailed, 0, true);
            }
        } else if (SparkDataControl::switchTuner(queuedTunerEnabled_)) {
            tunerRequestSent_ = true;
            tunerRequestEnabled_ = queuedTunerEnabled_;
            tunerRequestSentAtMs_ = millis();
            persistentEventLog.record(PersistentEvent::TunerSend, 1);
            Serial.printf("Controller: requesting tuner %s\n",
                          queuedTunerEnabled_ ? "entry" : "exit");
        } else {
            Serial.printf("Controller: tuner %s command failed\n",
                          queuedTunerEnabled_ ? "entry" : "exit");
            persistentEventLog.record(PersistentEvent::TunerFailed, queuedTunerEnabled_ ? 1 : 0, true);
        }
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
