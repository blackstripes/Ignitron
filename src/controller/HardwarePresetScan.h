#pragma once
#include <stdint.h>

// Idle-only, one outstanding slot read. Callers own response validation and
// command priority. Retry a bounded burst, then revisit missing slots after a
// cooldown: a single lost response must never disable names until reboot.
class HardwarePresetScan {
public:
    void reset() { *this = HardwarePresetScan{}; }
    template<class Missing, class Send, class Cancel>
    void tick(uint32_t now, uint8_t count, Missing missing, Send send, Cancel cancel) {
        if (cooling_) {
            if (uint32_t(now - sentAt_) < 30000) return;
            cooling_ = false;
            next_ = 1;
        }
        if (slot_) {
            if (!missing(slot_)) {
                cancel();
                next_ = slot_ + 1;
                slot_ = attempts_ = 0;
            } else {
                if (uint32_t(now - sentAt_) < 5000) return;
                // Release the transport even on the FINAL timeout, before
                // moving to another slot. Otherwise every later send fails.
                cancel();
                if (attempts_ < 3) {
                    ++attempts_;
                    send(slot_);
                    sentAt_ = now;
                    return;
                }
                next_ = slot_ + 1;
                slot_ = attempts_ = 0;
            }
        }
        for (; next_ <= count; ++next_) {
            if (!missing(next_)) continue;
            slot_ = next_;
            attempts_ = 1;
            send(slot_);
            sentAt_ = now;
            return;
        }
        cooling_ = true;
        sentAt_ = now;
    }
private:
    uint8_t slot_ = 0, attempts_ = 0, next_ = 1;
    uint32_t sentAt_ = 0;
    bool cooling_ = false;
};
