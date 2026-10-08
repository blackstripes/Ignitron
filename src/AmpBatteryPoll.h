#pragma once

#include "SparkSubmission.h"
#include <cstdint>

// Controller-task scheduler for the optional 02/71 amp battery query. The
// caller supplies the live transport snapshot; only an accepted send consumes
// the due window. Trace callbacks are optional and contain no payload.
class AmpBatteryPoll {
public:
    static constexpr uint32_t intervalMs = 60000;
    struct Transport {
        SparkSubmission submission;
        bool remaining;
        int remainingParts;
        bool laneActive;
        uint8_t ownerMessage, ownerSubcommand;
        bool controllerWorkPending;
    };
    enum class Event { Due, Deferred, Sent };

    void reset() { dueLogged_ = deferredLogged_ = false; }

    template <typename Snapshot, typename Send, typename Trace>
    void service(uint32_t now, bool connected, bool foregroundReady, uint32_t &lastAmpBatteryUpdate,
                  Snapshot snapshot, Send send, Trace trace) {
        if (lastAmpBatteryUpdate != 0 && now - lastAmpBatteryUpdate <= intervalMs) {
            reset();
            return;
        }
        if (!dueLogged_) { trace(Event::Due, snapshot()); dueLogged_ = true; }
        if (!connected) return;
        const Transport before = snapshot();
        if (foregroundReady && !before.remaining && !before.laneActive && send()) {
            lastAmpBatteryUpdate = now;
            trace(Event::Sent, snapshot());
            reset();
            return;
        }
        // After a failed submission, report the event-time result, not the
        // previous command's status. Never spin on an unchanged owner.
        Transport blocked = (!foregroundReady || before.remaining || before.laneActive) ? before : snapshot();
        blocked.controllerWorkPending = !foregroundReady;
        if (!deferredLogged_ || !same(blocked, deferred_)) {
            trace(Event::Deferred, blocked);
            deferred_ = blocked;
            deferredLogged_ = true;
        }
    }

private:
    static bool same(const Transport &a, const Transport &b) {
        return a.submission == b.submission && a.remaining == b.remaining &&
               a.remainingParts == b.remainingParts && a.laneActive == b.laneActive &&
                a.ownerMessage == b.ownerMessage && a.ownerSubcommand == b.ownerSubcommand &&
                a.controllerWorkPending == b.controllerWorkPending;
    }
    bool dueLogged_ = false, deferredLogged_ = false;
    Transport deferred_{};
};
