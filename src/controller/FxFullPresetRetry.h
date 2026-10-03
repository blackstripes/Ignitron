#pragma once
#include <cstdint>

// FX verification only: query after a short ACK grace period even when the
// command ACK is silent. Allow fragmented replies a full response window before
// replacement. Preset refresh uses the separate two-second schedule.
class FxFullPresetRetry {
public:
    static constexpr uint32_t kAckGraceMs = 1000;
    static constexpr uint32_t kResponseWindowMs = 5000;
    static constexpr uint32_t kDeadlineMs = 15000;

    void reset() { attempted_ = false; revoke(); }
    bool attempted() const { return attempted_; }
    uint8_t messageNumber() const { return message_; }
    bool querySentAfterAck() const { return message_ != 0 && sentAfterAck_; }
    bool expired(uint32_t now, uint32_t commandSentAt) const {
        return uint32_t(now - commandSentAt) >= kDeadlineMs;
    }
    bool due(uint32_t now, uint32_t commandSentAt) const {
        return !expired(now, commandSentAt) &&
            (!attempted_ ? uint32_t(now - commandSentAt) >= kAckGraceMs :
                           uint32_t(now - lastAttemptMs_) >= kResponseWindowMs);
    }
    void attemptedAt(uint32_t now, bool sent, uint8_t message, uint32_t revisionBeforeSend,
                     bool ackReceived) {
        attempted_ = true;
        lastAttemptMs_ = now;
        revoke();
        if (sent && message != 0) {
            message_ = message;
            revision_ = revisionBeforeSend;
            sentAfterAck_ = ackReceived;
        }
    }
    void revoke() { message_ = 0; sentAfterAck_ = false; }
    bool matches(uint8_t message, uint32_t revision) const {
        return message_ != 0 && message == message_ && revision != revision_;
    }

private:
    bool attempted_ = false;
    uint32_t lastAttemptMs_ = 0;
    uint32_t revision_ = 0;
    uint8_t message_ = 0;
    bool sentAfterAck_ = false;
};
