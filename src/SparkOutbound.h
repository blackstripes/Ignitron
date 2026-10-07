#pragma once

#include <cstdint>
#include <deque>
#include <vector>
#include "SparkMessageSequence.h"

// Protocol ACK lane: never enters the ordinary owner or its bookkeeping.
// The ACK's builder-supplied wire sequence is not changed by cursor advance.
template <typename Part, typename Write>
bool writeSparkProtocolAck(const std::vector<Part> &parts, uint8_t &nextMessageNum,
                           Write write) {
    nextMessageNum = nextNormalSparkMessageNumber(nextMessageNum);
    if (parts.empty()) return false;
    for (const Part &part : parts) {
        if (!write(part)) return false;
    }
    return true;
}

// Owns only the not-yet-written parts of one outbound command. Completion of
// the final write frees this lane; final ACK/response ownership is separate.
// Intermediate ACKs expose only the message number/opcode, so duplicate ACKs
// for one multipart command cannot be distinguished from the next part's ACK.
template <typename Part>
class SparkOutbound {
public:
    bool hasRemaining() const { return !remaining_.empty(); }
    size_t remainingCount() const { return remaining_.size(); }
    // Valid inside the write callback, before sendNext removes the part.
    bool writingLastPart() const { return remaining_.size() == 1; }
    void clear() { remaining_.clear(); lastWrittenValid_ = false; }

    template <typename Write>
    bool start(const std::vector<Part> &parts, Write write) {
        if (hasRemaining()) return false;
        remaining_.assign(parts.begin(), parts.end());
        lastWrittenValid_ = false;
        return sendNext(write);
    }

    template <typename Write>
    bool sendNext(Write write) {
        if (remaining_.empty()) return false;
        if (!write(remaining_.front())) {
            // BLE failed and disconnects the link. Replaying a partial
            // multipart command would be unsafe; release all unsent parts.
            clear();
            return false;
        }
        lastWritten_ = remaining_.front();
        lastWrittenValid_ = true;
        remaining_.pop_front();
        return true;
    }

    template <typename Ack, typename Write>
    bool onIntermediateAck(const Ack &ack, Write write) {
        // Only multipart preset uploads use 05/01 to release a part. Match
        // the wire sequence (Spark encodes zero as one), not just the opcode.
        if (!hasRemaining() || !lastWrittenValid_ || ack.cmd != 0x05 ||
            ack.subcmd != 0x01 || lastWritten_.cmd != 0x01 ||
            lastWritten_.subcmd != 0x01 ||
            ack.msgNum != (lastWritten_.msgNum == 0 ? 1 : lastWritten_.msgNum)) return false;
        return sendNext(write);
    }

private:
    std::deque<Part> remaining_;
    Part lastWritten_{};
    bool lastWrittenValid_ = false;
};
