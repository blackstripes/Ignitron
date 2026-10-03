#pragma once
#include <cstdint>

// Shared cadence for a post-switch full read and its startup-sync recovery.
// Correlation/publication gates remain owned by ControllerActions and Spark.
class FullPresetRetry {
public:
    static constexpr uint32_t kIntervalMs = 2000;

    void reset() { attempted_ = false; queryDispatched_ = false; revoke(); }
    bool attempted() const { return attempted_; }
    bool queryDispatched() const { return queryDispatched_; }
    bool due(uint32_t now) const {
        return !attempted_ || uint32_t(now - lastAttemptMs_) >= kIntervalMs;
    }
    void attemptedAt(uint32_t now, bool sent, uint8_t message, uint32_t revisionBeforeSend) {
        attempted_ = true;
        lastAttemptMs_ = now;
        revoke();
        if (sent && message != 0) {
            message_ = message;
            revision_ = revisionBeforeSend;
            queryDispatched_ = true;
        }
    }
    void revoke() { message_ = 0; }
    bool matches(uint8_t message, uint32_t revision) const {
        return message_ != 0 && message == message_ && revision != revision_;
    }

private:
    bool attempted_ = false;
    bool queryDispatched_ = false;
    uint32_t lastAttemptMs_ = 0;
    uint32_t revision_ = 0;
    uint8_t message_ = 0;
};
