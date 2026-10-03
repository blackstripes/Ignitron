#include "controller/ControllerActions.h"

#include "controller/ControllerState.h"
#include "controller/PresetRequestGate.h"
#include "controller/PresetLinkReset.h"
#include "controller/ProtocolObservations.h"
#include "SparkDataControl.h"
#include "SparkPresetControl.h"
#include "SparkStatus.h"
#include "PersistentEventLog.h"

#ifdef PANELAN_PRESET_TRACE
// One parseable line per milestone; id links a request to its outcome. No tone
// identity or protocol payload is printed by this trace.
#define PRESET_TRACE(fmt, ...) Serial.printf("PRESET_TRACE t=%lu " fmt "\n", static_cast<unsigned long>(millis()), ##__VA_ARGS__)
#else
#define PRESET_TRACE(...) do {} while (0)
#endif

bool ControllerActions::requestHardwarePreset(uint8_t preset) {
    const ControllerSnapshot &snapshot = state_.snapshot();
    const uint8_t maxHardwarePreset =
        static_cast<uint8_t>(SparkPresetControl::getInstance().numberOfHWBanks() * PRESETS_PER_BANK);
    const bool busy = presetSelectionMayProceedDuringSync(sentPreset_ != 0, presetTargets_.queued() != 0,
                                                           awaitingPresetFullResponse_ || fullPresetRetry_.attempted(),
                                                           startupFullPresetQueryIssued_,
                                                           snapshot.fullPresetObservedForLink);
    const char *rejection = preset < 1 || preset > maxHardwarePreset ? "range" :
        hasPendingFxOperation() ? "fx" : queuedTunerRequest_ || tunerRequestSent_ ? "tuner" :
        queuedLooperAction_ != LooperAction::None || sentLooperAction_ != LooperAction::None ? "looper" :
        busy && (!SparkDataControl::isAmpConnected() || !snapshot.identityKnown) ? "phase" :
        !busy && snapshot.connectionPhase != ControllerConnectionPhase::Ready ? "phase" :
        !busy && snapshot.sparkStateStale ? "stale" :
        !busy && preset == snapshot.confirmedHardwarePreset ? "already_current" : nullptr;
    if (rejection != nullptr) {
        PRESET_TRACE("event=reject target=%u confirmed=%u reason=%s phase=%u", preset,
                     snapshot.confirmedHardwarePreset, rejection, static_cast<unsigned>(snapshot.connectionPhase));
        return false;
    }
    // Revoke the previous full response before it can update active Spark data.
    // A newer selection makes that payload obsolete even if the wire reply is
    // already in transit.
    SparkDataControl::expectControllerFullPreset(0);
    state_.expectStartupFullPreset(0);
    if (awaitingPresetFullResponse_) awaitingPresetFullResponse_ = false;
    // The old startup query is obsolete as well. If the new intent is already
    // the reported slot, the synchronizer must issue a fresh full query.
    startupFullPresetQueryIssued_ = false;
    fullPresetRetry_.reset();
#ifdef PANELAN_PRESET_TRACE
    const uint32_t id = ++presetTraceNextId_;
    presetTracePhase_ = static_cast<uint8_t>(snapshot.connectionPhase);
    presetTraceFailedTarget_ = 0;
    presetTraceSatisfiedTarget_ = 0;
    presetTraceStartupId_ = 0;
    presetTraceStartupTarget_ = 0;
    // Keep a timed-out refresh's message id for a late diagnostic response.
#endif
    PRESET_TRACE("event=accept id=%lu target=%u confirmed=%u", static_cast<unsigned long>(id),
                 preset, snapshot.confirmedHardwarePreset);
    const bool defer = sentPreset_ != 0;
    presetTargets_.select(preset, defer);
#ifdef PANELAN_PRESET_TRACE
    if (defer) { presetTraceDeferredId_ = id; presetTraceDeferredAtMs_ = millis(); }
    else { presetTraceId_ = id; presetTraceStartedAtMs_ = millis(); }
#endif
    if (!defer) {
        presetBeforeRequest_ = snapshot.confirmedHardwarePreset;
    }
    // Pending is the latest requested destination, not a sent/confirmed value.
    state_.beginHardwarePresetRequest(preset);
    state_.invalidatePresetData();
    PRESET_TRACE("event=queued id=%lu target=%u elapsed=0", static_cast<unsigned long>(id), preset);
    return true;
}

bool ControllerActions::requestFxToggle(uint8_t slot) {
    const ControllerSnapshot &snapshot = state_.snapshot();
    if (slot >= snapshot.fxSlots.size() || snapshot.connectionPhase != ControllerConnectionPhase::Ready ||
        snapshot.sparkStateStale || snapshot.pendingHardwarePreset != 0 || sentPreset_ != 0 ||
        presetTargets_.queued() != 0 || hasPendingFxOperation() || queuedTunerRequest_ || tunerRequestSent_) {
        return false;
    }

    const ControllerFxSlot &fx = snapshot.fxSlots[slot];
    if (!fx.known || fx.modelName.empty()) {
        return false;
    }

    fxFullPresetRetry_.reset();
    fxAckReceived_ = false;
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
        presetTargets_.queued() != 0 || hasPendingFxOperation() || queuedTunerRequest_ || tunerRequestSent_) {
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
            !snapshot.looperPending && snapshot.pendingHardwarePreset == 0 && sentPreset_ == 0 && presetTargets_.queued() == 0 &&
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
        // The failed command leaves the observed FX state uncertain, even if
        // its pending flag has been cleared. A raw query is not accepted by
        // PanelLan's full-preset publication gate. Let startup sync register
        // both expectations around its own query instead.
        state.invalidatePresetData();
        state.expectStartupFullPreset(0);
        SparkDataControl::expectControllerFullPreset(0);
        startupFullPresetQueryIssued_ = false;
        startupFullPresetQueryMessageNumber_ = 0;
        fullPresetRetry_.reset();
    }
}

void ControllerActions::clearFxRequest() {
    queuedFxSlot_ = kNoFxSlot;
    sentFxSlot_ = kNoFxSlot;
    queuedFxDesiredEnabled_ = false;
    sentFxDesiredEnabled_ = false;
    fxEnabledBeforeRequest_ = false;
    queuedFxModelName_.clear();
    sentFxModelName_.clear();
    fxChainIdentityBeforeRequest_.clear();
    fxSentAtMs_ = 0;
    fxModelObservationRevisionBeforeRequest_ = 0;
    fxFullPresetObservationRevisionBeforeRequest_ = 0;
    fxSentAfterAckRevision_ = 0;
    sentFxMessageNumber_ = 0;
    fxAckReceived_ = false;
    if (fxFullPresetRetry_.attempted()) SparkDataControl::expectControllerFullPreset(0);
    fxFullPresetRetry_.reset();
}

void ControllerActions::onAmpDisconnected() {
#ifdef PANELAN_PRESET_TRACE
    if (presetTraceConnectionObserved_) PRESET_TRACE("event=disconnect");
    presetTraceConnectionObserved_ = false;
#endif
    SparkDataControl::cancelHWPresetRead();
    cacheScan_.reset();
    cacheMetadataRequested_ = false;
    cacheMetadataAtMs_ = 0;
    currentPresetQueryIssued_ = false;
    currentPresetQueryAtMs_ = 0;
    startupFullPresetQueryIssued_ = false;
    fullPresetRetry_.reset();
    startupFullPresetQueryMessageNumber_ = 0;
#ifdef PANELAN_PRESET_TRACE
    presetTraceStartupId_ = 0;
    presetTraceStartupTarget_ = 0;
    presetTraceSatisfiedTarget_ = 0;
#endif
    state_.expectStartupFullPreset(0);
    if (presetTargets_.queued() != 0) {
        PRESET_TRACE("event=fail id=%lu target=%u reason=disconnect_queued elapsed=%lu",
                     static_cast<unsigned long>(presetTraceId_), presetTargets_.queued(),
                     static_cast<unsigned long>(millis() - presetTraceStartedAtMs_));
#ifdef PANELAN_PRESET_TRACE
        presetTraceFailedTarget_ = presetTargets_.queued();
        presetTraceFailedId_ = presetTraceId_;
#endif
    }
    resetPresetLinkPending(presetTargets_, presetTimeoutReconcile_);
    reconciledPresetNumber_ = 0;
    presetReconcileQueryAtMs_ = 0;
    presetReconcileQueryAttempted_ = false;
    SparkDataControl::expectControllerFullPreset(0);
    if (sentPreset_ != 0) {
        PRESET_TRACE("event=fail id=%lu target=%u reason=disconnect elapsed=%lu",
                     static_cast<unsigned long>(presetTraceId_), sentPreset_,
                     static_cast<unsigned long>(millis() - presetTraceStartedAtMs_));
#ifdef PANELAN_PRESET_TRACE
        presetTraceFailedTarget_ = sentPreset_;
        presetTraceFailedId_ = presetTraceId_;
        presetTraceFailedFullMsg_ = awaitingPresetFullResponse_ ? presetFullQueryMessageNumber_ : 0;
        presetTraceFailedFullRevision_ = SparkDataControl::fullPresetObservationRevision();
        presetTraceFailedFullTarget_ = presetFullTarget_;
        presetTraceFailedFullId_ = presetTraceId_;
#endif
        state_.failHardwarePresetRequest();
        SparkDataControl::recordControllerPresetFailure();
        persistentEventLog.record(PersistentEvent::PresetFailed, sentPreset_, true);
    }
    sentPreset_ = 0;
    presetBeforeRequest_ = 0;
    sentAtMs_ = 0;
    sentAfterAckRevision_ = 0;
    numberVerification_.reset();
    awaitingPresetFullResponse_ = false;
    presetFullQueryMessageNumber_ = 0;
    presetFullTarget_ = 0;
    queuedTunerRequest_ = false;
    tunerRequestSent_ = false;
    tunerEntryCancelRequested_ = false;
    tunerRequestSentAtMs_ = 0;
    queuedLooperAction_ = LooperAction::None;
    sentLooperAction_ = LooperAction::None;
    looperSentAtMs_ = 0;
    looperSyncRequestedAtMs_ = 0;
    looperCommandRevisionBeforeRequest_ = 0;
    looperStatusRevisionBeforeRequest_ = 0;
    state_.disarmLooperClear();
    // State already marks the rendered FX stale. Preserve the existing failure
    // event for an unconfirmed command, but never query the disconnected amp.
    if (hasPendingFxOperation()) cancelFxRequest(state_, nullptr, false, "BLE disconnected");
    fxFullPresetRetry_.reset();
    fxSentAtMs_ = 0;
}

void ControllerActions::process(SparkDataControl &dataControl) {
    const ControllerSnapshot &snapshot = state_.snapshot();
    if (!SparkDataControl::isAmpConnected()) {
        onAmpDisconnected();
        return;
    }
#ifdef PANELAN_PRESET_TRACE
    presetTraceConnectionObserved_ = true;
    // Snapshot changes are Spark observations, not locally inferred success.
    // Retain the most recent failed target to label a later observation as late,
    // never as a successful controller confirmation.
    if (startupFullPresetQueryIssued_ &&
        SparkDataControl::fullPresetObservationRevision() != presetTraceStartupRevision_) {
        presetTraceStartupRevision_ = SparkDataControl::fullPresetObservationRevision();
        const bool match = SparkDataControl::fullPresetObservationMessageNumber() == startupFullPresetQueryMessageNumber_;
        PRESET_TRACE("event=startup_full_result id=%lu target=%u msg=%u sent=1 match=%u ready=%u",
                     static_cast<unsigned long>(presetTraceStartupId_), presetTraceStartupTarget_,
                     SparkDataControl::fullPresetObservationMessageNumber(), match,
                     match && snapshot.fullPresetObservedForLink &&
                         snapshot.confirmedHardwarePreset == presetTraceStartupTarget_);
    }
    if (SparkDataControl::isAmpConnected() && snapshot.confirmedHardwarePreset != presetTraceObserved_) {
        PRESET_TRACE("event=number id=%lu target=%u confirmed=%u", static_cast<unsigned long>(presetTraceId_),
                      sentPreset_ ? sentPreset_ : presetTargets_.queued(), snapshot.confirmedHardwarePreset);
        presetTraceObserved_ = snapshot.confirmedHardwarePreset;
        if (presetTraceFailedTarget_ != 0 && snapshot.confirmedHardwarePreset == presetTraceFailedTarget_) {
            PRESET_TRACE("event=late_number id=%lu target=%u confirmed=%u", static_cast<unsigned long>(presetTraceFailedId_),
                         presetTraceFailedTarget_, snapshot.confirmedHardwarePreset);
            presetTraceFailedTarget_ = 0;
        }
    }
    if (presetTraceFailedFullMsg_ != 0 &&
        SparkDataControl::fullPresetObservationRevision() != presetTraceFailedFullRevision_) {
        presetTraceFailedFullRevision_ = SparkDataControl::fullPresetObservationRevision();
        PRESET_TRACE("event=late_full_result id=%lu target=%u msg=%u match=%u confirmed=%u",
                     static_cast<unsigned long>(presetTraceFailedFullId_), presetTraceFailedFullTarget_,
                     SparkDataControl::fullPresetObservationMessageNumber(),
                     SparkDataControl::fullPresetObservationMessageNumber() == presetTraceFailedFullMsg_,
                     snapshot.confirmedHardwarePreset);
        if (SparkDataControl::fullPresetObservationMessageNumber() == presetTraceFailedFullMsg_)
            presetTraceFailedFullMsg_ = 0;
    }
    if (sentPreset_ != 0 || presetTargets_.queued() != 0) {
        const uint8_t phase = static_cast<uint8_t>(snapshot.connectionPhase);
        if (phase != presetTracePhase_) {
            PRESET_TRACE("event=phase id=%lu target=%u phase=%u confirmed=%u elapsed=%lu",
                          static_cast<unsigned long>(presetTraceId_), sentPreset_ ? sentPreset_ : presetTargets_.queued(),
                         phase, snapshot.confirmedHardwarePreset,
                         static_cast<unsigned long>(millis() - presetTraceStartedAtMs_));
            presetTracePhase_ = phase;
        }
    }
#endif
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
            dataControl.requestLooperSync();
            sentLooperAction_ = LooperAction::None;
        }
        return;
    }

    if (queuedLooperAction_ != LooperAction::None) {
        const LooperAction action = queuedLooperAction_;
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
            queuedLooperAction_ = LooperAction::None;
            sentLooperAction_ = action;
            looperSentAtMs_ = millis();
            looperCommandRevisionBeforeRequest_ = commandRevisionBeforeSend;
            looperStatusRevisionBeforeRequest_ = statusRevisionBeforeSend;
        }
        else if (!keepQueuedSparkIntent(SparkDataControl::lastSubmissionStatus())) {
            queuedLooperAction_ = LooperAction::None;
            state_.failLooperRequest();
        }
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
        if (!queuedTunerEnabled_) {
            // Spark 2 accepted the native 0x01/0x65-off command in testing
            // but did not consistently emit TUNER_OFF. The project's normal
            // tuner-off path makes the same request and restores preset mode;
            // a later fresh tuner output still wins and re-enters tuner.
            SparkDataControl::switchSubMode(SUB_MODE_PRESET);
            if (keepQueuedSparkIntent(SparkDataControl::lastSubmissionStatus())) return;
            if (SparkDataControl::lastSubmissionStatus() == SparkSubmission::Sent)
                Serial.println("Controller: requested tuner exit via preset mode");
            queuedTunerRequest_ = false;
            return;
        }
        if (SparkDataControl::switchTuner(queuedTunerEnabled_)) {
            queuedTunerRequest_ = false;
            tunerRequestSent_ = true;
            tunerRequestEnabled_ = queuedTunerEnabled_;
            tunerRequestSentAtMs_ = millis();
            Serial.printf("Controller: requesting tuner %s\n",
                          queuedTunerEnabled_ ? "entry" : "exit");
        } else if (!keepQueuedSparkIntent(SparkDataControl::lastSubmissionStatus())) {
            queuedTunerRequest_ = false;
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
        dataControl.requestLooperSync();
        looperSyncRequestedAtMs_ = millis();
        return;
    }

    if (presetTimeoutReconcile_.needed()) {
        if (presetTimeoutReconcile_.queryOutstanding() &&
            millis() - presetReconcileQueryAtMs_ >= kPresetTimeoutMs)
            presetTimeoutReconcile_.require(); // Expired replies cannot release the gate.
        uint8_t number = 0, cmd = 0, subcmd = 0, msg = 0;
        while (SparkDataControl::nextHardwareNumber(presetTimeoutReconcile_.cursor(), number, cmd, subcmd, msg)) {
            if (presetTimeoutReconcile_.observe(number, cmd, subcmd, msg, snapshot.confirmedHardwarePreset)) {
                presetBeforeRequest_ = number;
                reconciledPresetNumber_ = number;
                PRESET_TRACE("event=reconciled target=%u confirmed=%u msg=%u", presetTargets_.queued(), number, msg);
                return; // Dispatch only on the next tick, from the reconciled snapshot.
            }
        }
        if (!presetTimeoutReconcile_.queryOutstanding() &&
            (!presetReconcileQueryAttempted_ || millis() - presetReconcileQueryAtMs_ >= kPresetTimeoutMs)) {
            // Snapshot and unsolicited number updates are not reconciliation.
            // Capture the receive revision before the query is sent, and revoke
            // the old query on retry even when sending fails.
            presetTimeoutReconcile_.require();
            const uint32_t beforeSend = SparkDataControl::hardwareNumberRevision();
            uint8_t queryMessage = 0;
            const bool sent = dataControl.getCurrentPresetNum(&queryMessage);
            presetReconcileQueryAtMs_ = millis();
            presetReconcileQueryAttempted_ = true;
            if (sent) presetTimeoutReconcile_.startQuery(queryMessage, beforeSend);
            PRESET_TRACE("event=reconcile_query target=%u sent=%u msg=%u", presetTargets_.queued(), sent,
                         sent ? queryMessage : 0);
        }
        return;
    }

    // The legacy startup flow can fetch a complete current preset but not its
    // hardware-preset number. The controller cannot safely enable a preset
    // action until that separate Spark-owned value has been observed.
    if (sentPreset_ == 0 && presetTargets_.queued() == 0 && !awaitingPresetFullResponse_ && !hasPendingFxOperation() && snapshot.connectionPhase == ControllerConnectionPhase::Syncing &&
        snapshot.confirmedHardwarePreset == 0 &&
        (!currentPresetQueryIssued_ || millis() - currentPresetQueryAtMs_ >= kPresetTimeoutMs)) {
        Serial.println("Controller: requesting current hardware preset");
        currentPresetQueryIssued_ = dataControl.getCurrentPresetNum();
        if (currentPresetQueryIssued_) {
            currentPresetQueryAtMs_ = millis();
        }
        return;
    }

    if (sentPreset_ == 0 && presetTargets_.queued() == 0 && !awaitingPresetFullResponse_ && !hasPendingFxOperation() &&
        snapshot.connectionPhase == ControllerConnectionPhase::Syncing &&
        snapshot.confirmedHardwarePreset != 0 && !snapshot.fullPresetObservedForLink) {
        // PanelLan owns the startup full-preset request, avoiding races with
        // legacy cache restoration and giving the current link one authority.
        if (fullPresetRetry_.due(millis())) {
            // The two-second controller cadence does not supersede a still
            // fragmented transport response; wait for its five-second lane.
            if (startupFullPresetQueryIssued_ && dataControl.responseQueryPending(startupFullPresetQueryMessageNumber_, 0x01))
                return;
            const bool retry = fullPresetRetry_.queryDispatched();
#ifdef PANELAN_PRESET_TRACE
            if (startupFullPresetQueryIssued_)
                PRESET_TRACE("event=startup_full_timeout id=%lu target=%u msg=%u sent=1 match=0 ready=0",
                             static_cast<unsigned long>(presetTraceStartupId_), presetTraceStartupTarget_,
                             startupFullPresetQueryMessageNumber_);
            // Capture before sending, not after a possibly immediate observation.
            presetTraceStartupRevision_ = SparkDataControl::fullPresetObservationRevision();
            presetTraceStartupTarget_ = snapshot.confirmedHardwarePreset;
            presetTraceStartupId_ = presetTraceSatisfiedTarget_ == snapshot.confirmedHardwarePreset ? presetTraceId_ : 0;
#endif
            const uint32_t revisionBeforeSend = SparkDataControl::fullPresetObservationRevision();
            uint8_t newMessage = 0;
            const bool sent = dataControl.getCurrentPresetFromSpark(&newMessage);
            if (!sent && keepQueuedSparkIntent(dataControl.lastSubmissionStatus())) return;
            if (sent && retry) SparkDataControl::recordTransportRetry();
            state_.expectStartupFullPreset(0);
            SparkDataControl::expectControllerFullPreset(0);
            startupFullPresetQueryIssued_ = sent;
            if (sent) startupFullPresetQueryMessageNumber_ = newMessage;
            fullPresetRetry_.attemptedAt(millis(), startupFullPresetQueryIssued_,
                                         startupFullPresetQueryMessageNumber_, revisionBeforeSend);
            PRESET_TRACE("event=startup_full_query id=%lu target=%u msg=%u sent=%u match=0 ready=0 retry=%u",
                         static_cast<unsigned long>(presetTraceStartupId_), snapshot.confirmedHardwarePreset,
                         startupFullPresetQueryIssued_ ? startupFullPresetQueryMessageNumber_ : 0,
                         startupFullPresetQueryIssued_, retry);
            if (startupFullPresetQueryIssued_) {
                state_.expectStartupFullPreset(startupFullPresetQueryMessageNumber_);
                SparkDataControl::expectControllerFullPreset(startupFullPresetQueryMessageNumber_);
            }
            Serial.printf("Controller: requesting startup full preset (%s)\n",
                          startupFullPresetQueryIssued_ ? "sent" : "send failed");
        }
        return;
    }

    if (sentPreset_ != 0) {
        // A temporary unknown preset number moves the snapshot to Syncing even
        // while BLE is connected. Keep verifying the sent action; only link
        // loss (above), conflict, send failure or timeout can fail it.
        bool matchedQuery = false;
        uint8_t number = 0, cmd = 0, subcmd = 0, msg = 0;
        while (SparkDataControl::nextHardwareNumber(numberVerification_.cursor(), number, cmd, subcmd, msg))
            if (numberVerification_.observe(sentPreset_, number, cmd, subcmd, msg,
                                            snapshot.confirmedHardwarePreset)) matchedQuery = true;
        if (matchedQuery && snapshot.confirmedHardwarePreset == sentPreset_) {
            // Only a response to this command's post-send verification query
            // can confirm. A late 03/10 or unsolicited 03/38 still updates
            // the snapshot but cannot resolve the action.
            PRESET_TRACE("event=number_confirm id=%lu target=%u confirmed=%u elapsed=%lu",
                         static_cast<unsigned long>(presetTraceId_), sentPreset_, snapshot.confirmedHardwarePreset,
                         static_cast<unsigned long>(millis() - presetTraceStartedAtMs_));
            state_.confirmHardwarePresetRequest(sentPreset_);
            SparkDataControl::recordControllerPresetConfirm();
            persistentEventLog.record(PersistentEvent::PresetConfirmed, sentPreset_, true);
            state_.invalidatePresetData();
            if (presetTargets_.deferred() != 0) {
                presetTargets_.promote();
#ifdef PANELAN_PRESET_TRACE
                presetTraceId_ = presetTraceDeferredId_;
                presetTraceStartedAtMs_ = presetTraceDeferredAtMs_;
#endif
                presetBeforeRequest_ = snapshot.confirmedHardwarePreset;
                if (presetTargets_.queued() == snapshot.confirmedHardwarePreset) {
                    // Latest intent is already Spark's observed number. The
                    // startup synchronizer will fetch its full payload.
                    PRESET_TRACE("event=satisfied id=%lu target=%u confirmed=%u elapsed=%lu",
                                 static_cast<unsigned long>(presetTraceId_), presetTargets_.queued(),
                                 snapshot.confirmedHardwarePreset,
                                 static_cast<unsigned long>(millis() - presetTraceStartedAtMs_));
#ifdef PANELAN_PRESET_TRACE
                    presetTraceSatisfiedTarget_ = presetTargets_.queued();
#endif
                    presetTargets_.takeQueued();
                } else state_.beginHardwarePresetRequest(presetTargets_.queued());
                sentPreset_ = 0;
                numberVerification_.reset();
                return;
            }
            presetFullTarget_ = sentPreset_;
            const uint32_t revisionBeforeSend = SparkDataControl::fullPresetObservationRevision();
            awaitingPresetFullResponse_ = dataControl.getCurrentPresetFromSpark(&presetFullQueryMessageNumber_);
            fullPresetRetry_.attemptedAt(millis(), awaitingPresetFullResponse_,
                                         presetFullQueryMessageNumber_, revisionBeforeSend);
            PRESET_TRACE("event=full_query id=%lu target=%u sent=%u msg=%u elapsed=%lu",
                         static_cast<unsigned long>(presetTraceId_), sentPreset_, awaitingPresetFullResponse_,
                         awaitingPresetFullResponse_ ? presetFullQueryMessageNumber_ : 0,
                         static_cast<unsigned long>(millis() - presetTraceStartedAtMs_));
            if (awaitingPresetFullResponse_) {
                state_.expectStartupFullPreset(presetFullQueryMessageNumber_);
                SparkDataControl::expectControllerFullPreset(presetFullQueryMessageNumber_);
#ifdef PANELAN_PRESET_TRACE
                presetTraceFullRevision_ = revisionBeforeSend;
#endif
            } else {
                PRESET_TRACE("event=full_query_send_failed id=%lu target=%u elapsed=%lu",
                             static_cast<unsigned long>(presetTraceId_), sentPreset_,
                             static_cast<unsigned long>(millis() - presetTraceStartedAtMs_));
            }
            Serial.printf("Controller: preset %u number confirmed; refreshing full preset\n", sentPreset_);
            sentPreset_ = 0;
            numberVerification_.reset();
        } else if (snapshot.confirmedHardwarePreset != 0 && snapshot.confirmedHardwarePreset != presetBeforeRequest_ &&
                   snapshot.confirmedHardwarePreset != sentPreset_) {
            PRESET_TRACE("event=fail id=%lu target=%u reason=conflict_number confirmed=%u elapsed=%lu",
                         static_cast<unsigned long>(presetTraceId_), sentPreset_, snapshot.confirmedHardwarePreset,
                         static_cast<unsigned long>(millis() - presetTraceStartedAtMs_));
#ifdef PANELAN_PRESET_TRACE
            presetTraceFailedTarget_ = sentPreset_;
            presetTraceFailedId_ = presetTraceId_;
#endif
            // A deferred request still owns the pending display. Its promotion
            // below will begin the next command without clearing that intent.
            if (presetTargets_.deferred() == 0) state_.failHardwarePresetRequest();
            SparkDataControl::recordControllerPresetFailure();
            state_.invalidatePresetData();
            Serial.printf("Controller: preset conflict (Spark reports %u)\n", snapshot.confirmedHardwarePreset);
            sentPreset_ = 0;
            numberVerification_.reset();
            if (presetTargets_.deferred() != 0) {
                presetTargets_.promote();
#ifdef PANELAN_PRESET_TRACE
                presetTraceId_ = presetTraceDeferredId_;
                presetTraceStartedAtMs_ = presetTraceDeferredAtMs_;
#endif
                presetBeforeRequest_ = snapshot.confirmedHardwarePreset;
                state_.beginHardwarePresetRequest(presetTargets_.queued());
            }
        } else {
            AckData ack{};
            while (SparkDataControl::nextFinalAck(sentAfterAckRevision_, ack)) {
            PRESET_TRACE("event=ack id=%lu target=%u subtype=%u msg=%u elapsed=%lu",
                         static_cast<unsigned long>(presetTraceId_), sentPreset_, ack.subcmd, ack.msgNum,
                         static_cast<unsigned long>(millis() - presetTraceStartedAtMs_));
            }
            // The command timeout is anchored to the switch, not extended by
            // any query or reply. Poll even if Spark emits no ACK/broadcast.
            if (numberVerification_.shouldQuery(millis())) {
                // Spark NEO Core accepts a hardware-preset command without
                // necessarily sending an ACK or number broadcast. A query
                // reply is the only confirmation of its actual slot.
                Serial.println("Controller: verifying Spark preset number");
                const uint32_t beforeQuery = SparkDataControl::hardwareNumberRevision();
                uint8_t queryMessage = 0;
                const bool querySent = dataControl.getCurrentPresetNum(&queryMessage);
                if (querySent && numberVerification_.queryDispatched()) SparkDataControl::recordTransportRetry();
                PRESET_TRACE("event=number_query id=%lu target=%u sent=%u elapsed=%lu",
                              static_cast<unsigned long>(presetTraceId_), sentPreset_, querySent,
                              static_cast<unsigned long>(millis() - presetTraceStartedAtMs_));
                numberVerification_.attempted(querySent, queryMessage, beforeQuery, millis());
            }
            if (numberVerification_.expired(millis())) {
            PRESET_TRACE("event=fail id=%lu target=%u reason=number_timeout elapsed=%lu",
                         static_cast<unsigned long>(presetTraceId_), sentPreset_,
                         static_cast<unsigned long>(millis() - presetTraceStartedAtMs_));
#ifdef PANELAN_PRESET_TRACE
            presetTraceFailedTarget_ = sentPreset_;
            presetTraceFailedId_ = presetTraceId_;
#endif
            Serial.println("Controller: preset confirmation timed out; resyncing");
            if (presetTargets_.deferred() == 0) state_.failHardwarePresetRequest();
            SparkDataControl::recordControllerPresetFailure();
            SparkDataControl::expectControllerFullPreset(0);
            sentPreset_ = 0;
            numberVerification_.reset();
            if (presetTargets_.deferred() != 0) {
                presetTargets_.promote();
#ifdef PANELAN_PRESET_TRACE
                presetTraceId_ = presetTraceDeferredId_;
                presetTraceStartedAtMs_ = presetTraceDeferredAtMs_;
#endif
                presetTimeoutReconcile_.require();
                reconciledPresetNumber_ = 0;
                presetReconcileQueryAttempted_ = false;
                PRESET_TRACE("event=reconcile_wait target=%u", presetTargets_.queued());
            } else {
                dataControl.getCurrentPresetNum();
            }
            }
        }
        return;
    }

    if (awaitingPresetFullResponse_) {
#ifdef PANELAN_PRESET_TRACE
        if (SparkDataControl::fullPresetObservationRevision() != presetTraceFullRevision_) {
            presetTraceFullRevision_ = SparkDataControl::fullPresetObservationRevision();
            PRESET_TRACE("event=full_result id=%lu target=%u msg=%u match=%u confirmed=%u elapsed=%lu",
                         static_cast<unsigned long>(presetTraceId_), presetFullTarget_,
                         SparkDataControl::fullPresetObservationMessageNumber(),
                         SparkDataControl::fullPresetObservationMessageNumber() == presetFullQueryMessageNumber_,
                         snapshot.confirmedHardwarePreset,
                         static_cast<unsigned long>(millis() - presetTraceStartedAtMs_));
        }
#endif
        if (fullPresetRetry_.matches(SparkDataControl::fullPresetObservationMessageNumber(),
                                     SparkDataControl::fullPresetObservationRevision()) &&
            snapshot.confirmedHardwarePreset == presetFullTarget_) {
            PRESET_TRACE("event=full_refresh id=%lu target=%u msg=%u elapsed=%lu",
                         static_cast<unsigned long>(presetTraceId_), presetFullTarget_, presetFullQueryMessageNumber_,
                         static_cast<unsigned long>(millis() - presetTraceStartedAtMs_));
            awaitingPresetFullResponse_ = false;
            fullPresetRetry_.reset();
            SparkDataControl::expectControllerFullPreset(0);
        } else if (fullPresetRetry_.due(millis()) ||
                     (snapshot.confirmedHardwarePreset != 0 && snapshot.confirmedHardwarePreset != presetFullTarget_)) {
            const bool conflict = snapshot.confirmedHardwarePreset != 0 && snapshot.confirmedHardwarePreset != presetFullTarget_;
            if (!conflict && dataControl.responseQueryPending(presetFullQueryMessageNumber_, 0x01)) return;
            PRESET_TRACE("event=%s id=%lu target=%u msg=%u elapsed=%lu",
                         conflict ? "full_data_conflict" : "full_data_timeout",
                         static_cast<unsigned long>(presetTraceId_), presetFullTarget_, presetFullQueryMessageNumber_,
                         static_cast<unsigned long>(millis() - presetTraceStartedAtMs_));
            if (conflict) state_.invalidatePresetData();
#ifdef PANELAN_PRESET_TRACE
            presetTraceFailedFullMsg_ = presetFullQueryMessageNumber_;
            presetTraceFailedFullTarget_ = presetFullTarget_;
            presetTraceFailedFullRevision_ = SparkDataControl::fullPresetObservationRevision();
            presetTraceFailedId_ = presetTraceId_;
            presetTraceFailedFullId_ = presetTraceId_;
#endif
            awaitingPresetFullResponse_ = false;
            if (conflict) fullPresetRetry_.reset();
            else fullPresetRetry_.revoke();
            // Both gates must be revoked before startup sync can issue a new
            // query. A delayed reply to this obsolete message is not evidence.
            SparkDataControl::expectControllerFullPreset(0);
            state_.expectStartupFullPreset(0);
        }
        return;
    }

    if (sentFxSlot_ != kNoFxSlot) {
        if (sentFxSlot_ >= snapshot.fxSlots.size()) {
            cancelFxRequest(state_, &dataControl, true, "invalid FX slot");
            return;
        }
        // Bound the action from the original command, even if an ACK or a
        // response arrives in the tick that crosses the deadline.
        if (fxFullPresetRetry_.expired(millis(), fxSentAtMs_)) {
            cancelFxRequest(state_, &dataControl, true, "confirmation timed out");
            return;
        }

        const ControllerFxSlot &fx = snapshot.fxSlots[sentFxSlot_];
        // Drain ACK history before interpreting a full reply. An ACK received
        // after a query was sent cannot make that earlier query authoritative.
        AckData ack{};
        while (SparkDataControl::nextFinalAck(fxSentAfterAckRevision_, ack))
            if (ack.subcmd == 0x15 && ack.msgNum == sentFxMessageNumber_) fxAckReceived_ = true;
        const bool fullPresetObservedAfterSend =
            SparkDataControl::fullPresetObservationRevision() != fxFullPresetObservationRevisionBeforeRequest_;
        const bool matchingFullPreset = fullPresetObservedAfterSend &&
            fxFullPresetRetry_.matches(SparkDataControl::fullPresetObservationMessageNumber(),
                                       SparkDataControl::fullPresetObservationRevision());
#ifdef PANELAN_PRESET_TRACE
        if (fullPresetObservedAfterSend)
            PRESET_TRACE("event=fx_full_result slot=%u msg=%u match=%u ack=%u known=%u enabled=%u desired=%u chain=%u elapsed=%lu",
                          sentFxSlot_, SparkDataControl::fullPresetObservationMessageNumber(), matchingFullPreset,
                          matchingFullPreset && fxFullPresetRetry_.querySentAfterAck(),
                         fx.known, fx.enabled, sentFxDesiredEnabled_,
                         snapshot.fxChainIdentity == fxChainIdentityBeforeRequest_,
                         static_cast<unsigned long>(millis() - fxSentAtMs_));
#endif
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
        } else if (matchingFullPreset && fx.known && fx.modelName == sentFxModelName_ &&
                    fx.enabled == sentFxDesiredEnabled_ && fx.enabled != fxEnabledBeforeRequest_) {
            Serial.printf("Controller: FX %u (%s) confirmed by full preset response\n", sentFxSlot_, sentFxModelName_.c_str());
            state_.confirmFxToggleRequest(sentFxSlot_);
            SparkDataControl::recordControllerFxConfirm();
            persistentEventLog.record(PersistentEvent::FxConfirmed, sentFxSlot_, true);
            clearFxRequest();
        } else if (matchingFullPreset && fx.known && fx.modelName == sentFxModelName_ &&
                     fx.enabled != sentFxDesiredEnabled_ && fxFullPresetRetry_.querySentAfterAck()) {
            cancelFxRequest(state_, &dataControl, true, "full preset reported conflicting FX state");
        } else {
            // Neither ACK nor a pre-ACK old-state full reply confirms or
            // conflicts. Query after the grace period even without ACK, and
            // wait a full response window before replacing fragmented replies.
            if (fxFullPresetRetry_.due(millis(), fxSentAtMs_)) {
                // NEO may omit both ACK and FX_ONOFF. A still-owned fragmented
                // response must not be superseded by the retry cadence.
                if (fxFullPresetRetry_.attempted() &&
                    dataControl.responseQueryPending(fxFullPresetRetry_.messageNumber(), 0x01)) return;
                const uint32_t revisionBeforeSend = SparkDataControl::fullPresetObservationRevision();
                const bool retry = fxFullPresetRetry_.queryDispatched();
                uint8_t fxQueryMessage = 0;
                const bool sent = dataControl.getCurrentPresetFromSpark(&fxQueryMessage);
                if (!sent && keepQueuedSparkIntent(dataControl.lastSubmissionStatus())) return;
                if (sent && retry) SparkDataControl::recordTransportRetry();
                SparkDataControl::expectControllerFullPreset(0);
                fxFullPresetRetry_.attemptedAt(millis(), sent, fxQueryMessage, revisionBeforeSend,
                                                fxAckReceived_);
                if (sent) {
                    SparkDataControl::expectControllerFullPreset(fxQueryMessage);
                }
#ifdef PANELAN_PRESET_TRACE
                PRESET_TRACE("event=fx_full_query slot=%u msg=%u sent=%u first=%u ack=%u elapsed=%lu",
                              sentFxSlot_, sent ? fxQueryMessage : 0, sent, !retry, fxAckReceived_,
                             static_cast<unsigned long>(millis() - fxSentAtMs_));
#endif
                Serial.printf("Controller: FX %u verification full preset (%s)\n", sentFxSlot_,
                              sent ? "sent" : "send failed");
            }
        }
        return;
    }

    if (presetTargets_.queued() == 0) {
        // Preset and FX operations are serialized. An effect request captures
        // the exact Spark model/chain at tap time and cannot be retargeted.
        if (queuedFxSlot_ == kNoFxSlot) {
            processHardwareNameCache(dataControl);
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
        // An ACK can be recorded inside switchEffectOnOff(). Retain the
        // pre-send cursor so the next tick sees it, but not older ACKs.
        const uint32_t ackRevisionBeforeSend = SparkDataControl::finalAckRevision();
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
            fxSentAfterAckRevision_ = ackRevisionBeforeSend;
            sentFxMessageNumber_ = messageNumber;
            fxAckReceived_ = false;
            fxFullPresetRetry_.reset();
#ifdef PANELAN_PRESET_TRACE
            PRESET_TRACE("event=fx_sent slot=%u msg=%u desired=%u", sentFxSlot_, messageNumber,
                         sentFxDesiredEnabled_);
#endif
            SparkDataControl::recordControllerFxSend();
            persistentEventLog.record(PersistentEvent::FxSend, sentFxSlot_);
            Serial.printf("Controller: sending FX %u (%s) %s\n", sentFxSlot_, sentFxModelName_.c_str(),
                          sentFxDesiredEnabled_ ? "on" : "off");
        } else if (!keepQueuedSparkIntent(SparkDataControl::lastSubmissionStatus())) {
            cancelFxRequest(state_, &dataControl, true, "Spark command send failed");
        }
        return;
    }
    const uint8_t preset = presetTargets_.takeQueued();
    const uint8_t reconciliationBeforeSend = reconciledPresetNumber_;
    const bool satisfiedByReconciliation = reconciledPresetNumber_ == preset;
    const bool mustSendAfterReconciliation = reconciledPresetNumber_ != 0 && !satisfiedByReconciliation;
    reconciledPresetNumber_ = 0;
    if (preset == snapshot.confirmedHardwarePreset && !mustSendAfterReconciliation) {
        // An external HW report may have arrived while this intent was queued.
        // It is not evidence that a command we haven't sent was confirmed.
        PRESET_TRACE("event=satisfied id=%lu target=%u confirmed=%u elapsed=%lu",
                     static_cast<unsigned long>(presetTraceId_), preset, snapshot.confirmedHardwarePreset,
                     static_cast<unsigned long>(millis() - presetTraceStartedAtMs_));
#ifdef PANELAN_PRESET_TRACE
        presetTraceSatisfiedTarget_ = preset;
#endif
        state_.confirmHardwarePresetRequest(preset);
        return;
    }
    if (dataControl.changeHWPreset(preset)) {
        PRESET_TRACE("event=sent id=%lu target=%u elapsed=%lu", static_cast<unsigned long>(presetTraceId_),
                     preset, static_cast<unsigned long>(millis() - presetTraceStartedAtMs_));
        Serial.printf("Controller: sending preset %u\n", preset);
        sentPreset_ = preset;
        sentAtMs_ = millis();
        sentAfterAckRevision_ = SparkDataControl::finalAckRevision();
        numberVerification_.start(sentAtMs_, SparkDataControl::hardwareNumberRevision());
        awaitingPresetFullResponse_ = false;
        SparkDataControl::recordControllerPresetSend();
        persistentEventLog.record(PersistentEvent::PresetSend, preset);
    } else if (keepQueuedSparkIntent(SparkDataControl::lastSubmissionStatus())) {
        presetTargets_.select(preset, false);
        reconciledPresetNumber_ = reconciliationBeforeSend;
        return;
    } else {
        PRESET_TRACE("event=fail id=%lu target=%u reason=send elapsed=%lu", static_cast<unsigned long>(presetTraceId_),
                     preset, static_cast<unsigned long>(millis() - presetTraceStartedAtMs_));
#ifdef PANELAN_PRESET_TRACE
        presetTraceFailedTarget_ = preset;
        presetTraceFailedId_ = presetTraceId_;
#endif
        state_.failHardwarePresetRequest();
        SparkDataControl::recordControllerPresetFailure();
        persistentEventLog.record(PersistentEvent::PresetFailed, preset, true);
    }
}

void ControllerActions::processHardwareNameCache(SparkDataControl &dataControl) {
    const ControllerSnapshot &snapshot = state_.snapshot();
    SparkPresetControl &presets = SparkPresetControl::getInstance();
    if (cacheGeneration_ != presets.hardwareCacheGeneration()) {
        dataControl.cancelHWPresetRead();
        cacheGeneration_ = presets.hardwareCacheGeneration();
        cacheScan_.reset();
    }
    if (snapshot.connectionPhase != ControllerConnectionPhase::Ready || snapshot.sparkStateStale ||
        awaitingPresetFullResponse_)
        return;

    // Startup metadata queries were one-shot. A lost serial/checksum response
    // must not leave a Ready controller with permanently nameless minis.
    if (snapshot.ampSerial.empty() || !presets.hardwareChecksumsReady()) {
        if (!cacheMetadataRequested_ || uint32_t(millis() - cacheMetadataAtMs_) >= 5000) {
            if (snapshot.ampSerial.empty()) dataControl.getSerialNumber();
            else dataControl.getHWChecksums();
            cacheMetadataRequested_ = true;
            cacheMetadataAtMs_ = millis();
        }
        return;
    }
    cacheMetadataRequested_ = false;
    const uint8_t count = static_cast<uint8_t>(presets.numberOfHWBanks() * PRESETS_PER_BANK);
    cacheScan_.tick(millis(), count,
        [&](uint8_t slot) { return presets.isHWPresetMissing(slot); },
        [&](uint8_t slot) { return dataControl.readHWPreset(slot); },
        [&]() { dataControl.cancelHWPresetRead(); });
}
