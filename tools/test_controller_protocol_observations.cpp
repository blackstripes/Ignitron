#include "controller/ProtocolObservations.h"
#include "controller/PresetAckMatch.h"
#include <cassert>

struct Number { uint8_t slot, cmd, subcmd, msg; };
struct Ack { uint8_t subcmd, msgNum; };

int main() {
    ProtocolObservations<Number> numbers;
    uint32_t numberCursor = numbers.revision(); // switch to slot 2 sent
    // Switch away then back: reply to the earlier query arrives after the
    // new switch. It changes the snapshot but must not resolve the switch.
    numbers.record({2, 3, 0x10, 21});
    Number n{};
    assert(numbers.next(numberCursor, n));
    assert(!matchesHardwareNumberReply(0, n.cmd, n.subcmd, n.msg));
    // Even after issuing this switch's query, an earlier query and a broadcast
    // with the desired number are not confirmation.
    const uint8_t query = 42;
    numbers.record({2, 3, 0x10, 21});
    numbers.record({2, 3, 0x38, 42});
    while (numbers.next(numberCursor, n))
        assert(!matchesHardwareNumberReply(query, n.cmd, n.subcmd, n.msg));
    numbers.record({2, 3, 0x10, query});
    assert(numbers.next(numberCursor, n));
    assert(matchesHardwareNumberReply(query, n.cmd, n.subcmd, n.msg));

    ProtocolObservations<Ack> acks;
    uint32_t ackCursor = acks.revision(); // 0x38 command sent with msg 41
    acks.record({0x38, 41});
    acks.record({0x15, 44}); // same process interval, unrelated ACK last
    bool found = false;
    Ack a{};
    while (acks.next(ackCursor, a))
        found |= matchesHardwarePresetAck(41, a.subcmd, a.msgNum);
    assert(found);
    assert(!acks.next(ackCursor, a)); // consumed once
    // Overrun drops evidence rather than returning a fabricated match.
    ProtocolObservations<Ack, 2> bounded;
    uint32_t old = bounded.revision();
    bounded.record({0x38, 41});
    bounded.record({0x15, 1});
    bounded.record({0x15, 2});
    while (bounded.next(old, a)) assert(!matchesHardwarePresetAck(41, a.subcmd, a.msgNum));
}
