#include "../src/SparkOutbound.h"
#include "../src/SparkMessageSequence.h"

#include <cassert>
#include <cstdint>
#include <vector>

struct Part {
    uint8_t msgNum, cmd, subcmd, data;
};
struct Ack {
    uint8_t msgNum, cmd, subcmd;
};

int main() {
    SparkOutbound<Part> outbound;
    uint8_t next = 1;
    std::vector<uint8_t> writes;
    const auto write = [&](const Part &part) { writes.push_back(part.data); return true; };
    const auto ordinary = [&](const std::vector<Part> &parts) {
        if (outbound.hasRemaining()) return false;
        next = nextNormalSparkMessageNumber(next);
        return outbound.start(parts, write);
    };

    const std::vector<Part> a{{1, 1, 1, 11}, {1, 1, 1, 12}, {1, 1, 1, 13}};
    assert(ordinary(a));
    assert((writes == std::vector<uint8_t>{11}));
    assert(next == 2);
    assert(!ordinary({{2, 1, 0x38, 20}}));
    assert(next == 2 && outbound.hasRemaining());
    assert(!outbound.onIntermediateAck(Ack{2, 5, 1}, write));
    assert(!outbound.onIntermediateAck(Ack{1, 4, 1}, write));
    assert((writes == std::vector<uint8_t>{11}));

    // Protocol ACK uses the direct write lane, not the ordinary scheduler.
    const Part protocolAck{0x77, 4, 0x70, 90};
    assert(writeSparkProtocolAck(std::vector<Part>{protocolAck}, next, write));
    assert(next == 3 && outbound.hasRemaining());
    assert((writes == std::vector<uint8_t>{11, 90}));
    assert(outbound.onIntermediateAck(Ack{1, 5, 1}, write));
    assert((writes == std::vector<uint8_t>{11, 90, 12}));
    // ACKs carry no part index: a duplicate ACK is indistinguishable from the
    // next legitimate ACK and can release another part. This is a known
    // protocol limitation, not duplicate-ACK protection.
    assert(outbound.onIntermediateAck(Ack{1, 5, 1}, write));
    assert((writes == std::vector<uint8_t>{11, 90, 12, 13}));
    assert(!outbound.hasRemaining());
    assert(ordinary({{next, 1, 0x38, 30}}));
    assert((writes == std::vector<uint8_t>{11, 90, 12, 13, 30}));

    // An ordinary command, then ACK, then ordinary command uses the legacy
    // cursor progression even though the ACK carries its incoming wire seq.
    SparkOutbound<Part> simple;
    next = 1;
    assert(simple.start({{next, 1, 0x38, 40}}, write));
    next = nextNormalSparkMessageNumber(next);
    assert(writeSparkProtocolAck(std::vector<Part>{{0x55, 4, 0x70, 91}}, next, write));
    assert(next == 3);
    assert(writes[writes.size() - 1] == 91);
    assert(simple.start({{next, 1, 0x38, 41}}, write));
    assert(writes[writes.size() - 1] == 41);

    // Failed writes disconnect transport; an unsent part must not block
    // subsequent work after link recovery.
    SparkOutbound<Part> failed;
    assert(!failed.start(a, [](const Part &) { return false; }));
    assert(!failed.hasRemaining());
    assert(failed.start({{2, 1, 0x38, 20}}, write));

    // A first BLE write failure still consumes the sequence just as the
    // production trigger path does (busy rejection above does not).
    SparkOutbound<Part> failedFirst;
    next = 1;
    next = nextNormalSparkMessageNumber(next);
    assert(!failedFirst.start({{1, 1, 0x38, 22}},
                               [](const Part &) { return false; }));
    assert(next == 2);
    assert(!failedFirst.hasRemaining());

    // With no intermediate ACK the owner retains its remaining parts and
    // rejects unrelated requests without advancing their message cursor.
    SparkOutbound<Part> stalled;
    next = 1;
    assert(stalled.start(a, write));
    next = nextNormalSparkMessageNumber(next);
    assert(!stalled.start({{next, 1, 0x38, 21}}, write));
    assert(next == 2 && stalled.hasRemaining());

    SparkOutbound<Part> retry;
    const std::vector<Part> twoParts{{1, 1, 1, 50}, {1, 1, 1, 51}};
    assert(retry.start(twoParts, write));
    assert(!retry.onIntermediateAck(Ack{1, 5, 1}, [](const Part &) { return false; }));
    assert(!retry.hasRemaining());
    assert(retry.start({{2, 1, 0x38, 52}}, write));

    next = 3;
    assert(!writeSparkProtocolAck(std::vector<Part>{{0x66, 4, 1, 92}}, next,
                                  [](const Part &) { return false; }));
    assert(next == 4); // failure still consumes one cursor step, as before
}
