#pragma once

#include <cstdint>

// Only one Spark command may be in flight. Intent is latest-wins; replacing a
// queued (unsent) command is safe, while a sent command must be confirmed first.
class PresetTargetQueue {
public:
    void select(uint8_t target, bool commandSent) {
        if (commandSent) deferred_ = target;
        else queued_ = target;
    }
    uint8_t queued() const { return queued_; }
    uint8_t deferred() const { return deferred_; }
    uint8_t takeQueued() { const uint8_t target = queued_; queued_ = 0; return target; }
    uint8_t promote() { queued_ = deferred_; deferred_ = 0; return queued_; }
    void clear() { queued_ = deferred_ = 0; }
private:
    uint8_t queued_ = 0;
    uint8_t deferred_ = 0;
};
