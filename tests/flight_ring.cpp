#include "../src/FlightRing.h"
#include <cassert>

int main() {
    FlightRing::Store store{};
    assert(!FlightRing::valid(store.header));
    FlightRing::initialize(store);
    assert(FlightRing::valid(store.header));
    assert(store.header.overwritten == 0);
    for (unsigned i = 0; i < 70; ++i) FlightRing::append(store, i * 10, 3, 4, i);
    assert(store.header.count == 64 && FlightRing::at(store, 0).sequence == 7);
    assert(store.header.overwritten == 6 && FlightRing::valid(store.header));
    assert(FlightRing::at(store, 63).value == 69);
    for (unsigned i = 0; i < 64; ++i) {
        assert(FlightRing::matchesSnapshot(store.header, FlightRing::at(store, i), i));
        assert(FlightRing::expectedSequence(store.header, i) == i + 7);
    }
    const auto snapshot = store.header;
    FlightRing::append(store, 700, 3, 4, 70);
    assert(FlightRing::valid(store.records[snapshot.write]));
    assert(!FlightRing::matchesSnapshot(snapshot, store.records[snapshot.write], 0));
    assert(store.header.overwritten == 7);
    auto damaged = store.header;
    ++damaged.overwritten;
    assert(!FlightRing::valid(damaged)); // overwritten counter is CRC-protected
    damaged = store.header;
    damaged.layout = 1;
    damaged.crc = FlightRing::checksum(&damaged, offsetof(FlightRing::Header, crc));
    assert(!FlightRing::valid(damaged)); // v1 is incompatible even with a fresh CRC
    damaged = store.header;
    damaged.crc ^= 1;
    assert(!FlightRing::valid(damaged));
    FlightRing::Store previous = store;
    FlightRing::initialize(store, previous.header.boot + 1, previous.header.nextSequence);
    assert(store.header.overwritten == 0);
    FlightRing::append(store, 1, 1, 0, 0);
    assert(FlightRing::at(store, 0).boot == 2 && FlightRing::at(store, 0).sequence == 72);
    assert(FlightRing::at(previous, 0).boot == 1);
    previous.records[previous.header.write].value ^= 1;
    assert(!FlightRing::valid(FlightRing::at(previous, 0)));
    previous.header.layout++;
    assert(!FlightRing::valid(previous.header));
    previous.header.layout--;
    previous.header.count = 65;
    assert(!FlightRing::valid(previous.header));
    FlightRing::initialize(previous, 7, UINT32_MAX);
    FlightRing::append(previous, 1, 1, 0, 0);
    FlightRing::append(previous, 2, 1, 0, 0);
    assert(FlightRing::at(previous, 0).sequence == UINT32_MAX);
    assert(FlightRing::at(previous, 1).sequence == 1);
    assert(FlightRing::matchesSnapshot(previous.header, FlightRing::at(previous, 0), 0));
    assert(FlightRing::matchesSnapshot(previous.header, FlightRing::at(previous, 1), 1));
    FlightRing::initialize(previous, 7, UINT32_MAX - 2);
    for (unsigned i = 0; i < 70; ++i) FlightRing::append(previous, i, 1, 0, 0);
    for (unsigned i = 0; i < 64; ++i)
        assert(FlightRing::matchesSnapshot(previous.header, FlightRing::at(previous, i), i));
    FlightRing::initialize(previous);
    for (unsigned i = 0; i < unsigned(UINT16_MAX) + FlightRing::capacity + 2; ++i)
        FlightRing::append(previous, i, 1, 0, 0);
    assert(previous.header.overwritten == UINT16_MAX && FlightRing::valid(previous.header));
    FlightRing::append(previous, 0, 1, 0, 0);
    assert(previous.header.overwritten == UINT16_MAX && FlightRing::valid(previous.header));
    return 0;
}
