#include "controller/ControllerActions.h"

#include "controller/ControllerState.h"
#include "SparkDataControl.h"

bool ControllerActions::requestHardwarePreset(uint8_t preset) {
    const ControllerSnapshot &snapshot = state_.snapshot();
    if (preset < 1 || preset > 4 || snapshot.connectionPhase != ControllerConnectionPhase::Ready ||
        snapshot.pendingHardwarePreset != 0 || preset == snapshot.confirmedHardwarePreset) {
        return false;
    }
    queuedPreset_ = preset;
    presetBeforeRequest_ = snapshot.confirmedHardwarePreset;
    state_.beginHardwarePresetRequest(preset);
    return true;
}

void ControllerActions::process(SparkDataControl &dataControl) {
    const ControllerSnapshot &snapshot = state_.snapshot();
    if (snapshot.connectionPhase == ControllerConnectionPhase::Scanning ||
        snapshot.connectionPhase == ControllerConnectionPhase::Reconnecting ||
        snapshot.connectionPhase == ControllerConnectionPhase::Identifying) {
        currentPresetQueryIssued_ = false;
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

    if (queuedPreset_ == 0) {
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
