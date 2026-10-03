#include "controller/PresetNumberVerification.h"
#include "controller/ProtocolObservations.h"
#include <cassert>
#include <cstdint>

struct Number { uint8_t slot, cmd, subcmd, msg; };

int main() {
    ProtocolObservations<Number> numbers;
    PresetNumberVerification verification;
    assert(!verification.shouldQuery(100));
    assert(!verification.queryDispatched());
    numbers.record({1, 0x03, 0x10, 7}); // reply before switch
    verification.start(100, numbers.revision());
    // No ACK or broadcast: first poll is immediate, well inside 500 ms.
    assert(verification.shouldQuery(101));
    const uint32_t beforeFirst = numbers.revision();
    verification.attempted(true, 12, beforeFirst, 101);
    assert(verification.queryDispatched());
    auto drain = [&](uint8_t target, uint8_t snapshot, uint32_t now) {
        (void)now; // Models a delayed process tick; the receive record has no timestamp.
        Number n{};
        bool confirmed = false;
        while (numbers.next(verification.cursor(), n))
            if (verification.observe(target, n.slot, n.cmd, n.subcmd, n.msg, snapshot)) confirmed = true;
        return confirmed;
    };
    assert(!drain(2, 1, 102)); // old reply cannot be consumed
    numbers.record({2, 0x03, 0x38, 12}); // broadcast, not reply
    numbers.record({2, 0x03, 0x10, 11}); // stale reply to another query
    assert(!drain(2, 2, 103));
    assert(!verification.shouldQuery(600)); // no duplicate while outstanding
    numbers.record({1, 0x03, 0x10, 12}); // previous slot is not confirmation
    assert(!drain(2, 1, 200));
    assert(!verification.shouldQuery(600)); // bounded cadence after reply
    assert(verification.shouldQuery(601));
    const uint32_t beforeSecond = numbers.revision();
    verification.attempted(true, 13, beforeSecond, 601);
    numbers.record({2, 0x03, 0x10, 12}); // late reply to first poll
    assert(!drain(2, 2, 602));
    assert(!verification.shouldQuery(1000));
    assert(verification.shouldQuery(1101)); // silent reply: expire and repoll
    const uint32_t beforeThird = numbers.revision();
    verification.attempted(true, 14, beforeThird, 1101);
    numbers.record({2, 0x03, 0x10, 14});
    assert(!drain(2, 1, 1200)); // snapshot must agree with correlated reply
    numbers.record({2, 0x03, 0x10, 14});
    assert(!drain(2, 2, 1201)); // first reply retired this query identity
    assert(verification.shouldQuery(1601));
    verification.attempted(true, 15, numbers.revision(), 1601);
    numbers.record({2, 0x03, 0x10, 15});
    assert(drain(2, 2, 1700));
    verification.reset();
    assert(!verification.shouldQuery(3000));

    verification.start(4000, numbers.revision());
    assert(!verification.queryDispatched());
    assert(verification.shouldQuery(4000));
    verification.attempted(false, 16, numbers.revision(), 4000);
    assert(verification.attempted() && !verification.queryDispatched());
    numbers.record({2, 0x03, 0x10, 16});
    assert(!drain(2, 2, 4001)); // failed send has no query identity
    assert(!verification.shouldQuery(4499));
    assert(verification.shouldQuery(4500));
    verification.attempted(false, 17, numbers.revision(), 4500);
    assert(!verification.queryDispatched());
    assert(!verification.shouldQuery(4999));
    assert(verification.shouldQuery(5000));
    assert(!verification.expired(8999));
    assert(verification.expired(9000));
    assert(!verification.shouldQuery(9000)); // polls cannot extend switch deadline

    verification.start(10000, numbers.revision());
    verification.attempted(false, 0, numbers.revision(), 10000);
    assert(!verification.queryDispatched()); // first successful send is not a retry
    verification.attempted(true, 19, numbers.revision(), 10001);
    assert(verification.queryDispatched());
    numbers.record({2, 0x03, 0x10, 19});
    assert(drain(2, 2, 10501)); // queued timely response survives a late process tick
    verification.attempted(false, 0, numbers.revision(), 10502);
    assert(verification.queryDispatched()); // failed replacement does not erase history

    verification.start(UINT32_MAX - 101, numbers.revision());
    verification.attempted(false, 18, numbers.revision(), UINT32_MAX - 100);
    assert(!verification.shouldQuery(398));
    assert(verification.shouldQuery(399));
}
