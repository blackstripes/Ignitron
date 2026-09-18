#include "controller/ControllerState.h"

#include "SparkDataControl.h"
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
        wasLinkEstablished_ = false;
        publishIfChanged(next);
        return;
    }

    wasLinkEstablished_ = true;
    SparkStatus &status = SparkStatus::getInstance();
    next.ampName = status.ampName();
    next.ampSerial = status.ampSerialNumber();
    next.identityKnown = dataControl.ampNameReceived() && !next.ampName.empty();
    next.connectionPhase = next.identityKnown
                               ? ControllerConnectionPhase::Syncing
                               : ControllerConnectionPhase::Identifying;
    // The existing protocol stack has not yet completed an epoch-aware preset
    // resync, so no Spark-owned field is ready for performance controls.
    next.sparkStateStale = true;
    publishIfChanged(next);
}

void ControllerState::publishIfChanged(const ControllerSnapshot &next) {
    if (snapshot_.connectionPhase == next.connectionPhase &&
        snapshot_.sparkStateStale == next.sparkStateStale &&
        snapshot_.identityKnown == next.identityKnown &&
        snapshot_.ampName == next.ampName &&
        snapshot_.ampSerial == next.ampSerial) {
        return;
    }
    snapshot_ = next;
    ++snapshot_.revision;
}
