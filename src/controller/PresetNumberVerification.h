#pragma once
#include <cstdint>
#include "ProtocolObservations.h"

// Post-switch authoritative reads. A switch ACK/broadcast is not evidence;
// only a fresh, correlated 03/10 reply reporting the target can confirm it.
class PresetNumberVerification {
public:
    void reset() { active_ = false; queryMessage_ = 0; attempted_ = false; queryDispatched_ = false; }
    void start(uint32_t now, uint32_t revisionBeforeSwitch) {
        reset();
        active_ = true;
        startedMs_ = now;
        cursor_ = revisionBeforeSwitch;
    }
    uint32_t &cursor() { return cursor_; }
    bool attempted() const { return attempted_; }
    // True only after a verification query actually left the controller.
    bool queryDispatched() const { return queryDispatched_; }
    bool expired(uint32_t now) const { return active_ && uint32_t(now - startedMs_) >= 5000; }
    bool shouldQuery(uint32_t now) const {
        // An unanswered query expires after one cadence interval. Its message
        // identity is replaced on retry; a late reply cannot confirm the new one.
        return active_ && !expired(now) &&
               (!attempted_ || uint32_t(now - lastAttemptMs_) >= kPollMs);
    }
    void attempted(bool sent, uint8_t message, uint32_t revisionBeforeSend, uint32_t now) {
        attempted_ = true;
        lastAttemptMs_ = now;
        cursor_ = revisionBeforeSend;
        queryMessage_ = sent ? message : 0;
        if (sent) queryDispatched_ = true;
    }
    bool observe(uint8_t target, uint8_t number, uint8_t cmd, uint8_t subcmd,
                 uint8_t message, uint8_t snapshotNumber) {
        // Receive-history events have no timestamp. Drain a correlated reply
        // before a retry/timeout replaces its identity, even if this process
        // tick runs after a cadence boundary.
        if (!active_ || !matchesHardwareNumberReply(queryMessage_, cmd, subcmd, message)) return false;
        queryMessage_ = 0; // Even a previous-slot reply releases the next poll.
        return number == target && snapshotNumber == target;
    }
private:
    static constexpr uint32_t kPollMs = 500;
    bool active_ = false;
    bool attempted_ = false;
    bool queryDispatched_ = false;
    uint8_t queryMessage_ = 0;
    uint32_t cursor_ = 0;
    uint32_t startedMs_ = 0;
    uint32_t lastAttemptMs_ = 0;
};
