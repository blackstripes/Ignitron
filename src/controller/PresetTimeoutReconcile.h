#pragma once
#include <cstdint>
#include "ProtocolObservations.h"

// A timed-out switch must not allow a deferred switch to use a number left
// behind by an earlier query. Only a reply to a post-timeout query releases it.
class PresetTimeoutReconcile {
public:
    void reset() { needed_ = false; queryMessage_ = 0; }
    void require() { needed_ = true; queryMessage_ = 0; }
    bool needed() const { return needed_; }
    bool queryOutstanding() const { return queryMessage_ != 0; }
    uint32_t &cursor() { return cursor_; }
    void startQuery(uint8_t message, uint32_t revisionBeforeSend) {
        cursor_ = revisionBeforeSend;
        queryMessage_ = message;
    }
    bool observe(uint8_t number, uint8_t cmd, uint8_t subcmd, uint8_t message,
                 uint8_t snapshotNumber) {
        if (!needed_ || !matchesHardwareNumberReply(queryMessage_, cmd, subcmd, message) ||
            number == 0 || snapshotNumber != number) return false;
        reset();
        return true;
    }
private:
    bool needed_ = false;
    uint8_t queryMessage_ = 0;
    uint32_t cursor_ = 0;
};
