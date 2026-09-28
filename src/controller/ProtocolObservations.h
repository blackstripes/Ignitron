#pragma once
#include <cstdint>

// Bounded receive history. An overwritten event is a missed milestone, never
// evidence for another command. Consumers start at the revision before send.
template <typename T, unsigned Capacity = 32>
class ProtocolObservations {
public:
    uint32_t revision() const { return revision_; }
    void record(const T &value) { entries_[revision_++ % Capacity] = value; }
    bool next(uint32_t &cursor, T &value) const {
        if (cursor == revision_) return false;
        if (revision_ - cursor > Capacity) cursor = revision_ - Capacity;
        value = entries_[cursor++ % Capacity];
        return true;
    }
private:
    T entries_[Capacity]{};
    uint32_t revision_ = 0;
};

// 03/10 is a reply to 02/10; 03/38 is an unsolicited switch broadcast.
// A broadcast may update the visible slot, but is never command evidence.
inline bool matchesHardwareNumberReply(uint8_t outstandingQuery, uint8_t cmd,
                                       uint8_t subcmd, uint8_t messageNumber) {
    return outstandingQuery != 0 && cmd == 0x03 && subcmd == 0x10 &&
           messageNumber == outstandingQuery;
}
