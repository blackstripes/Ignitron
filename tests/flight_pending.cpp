#include "../src/FlightPending.h"
#include <cassert>
#include <cstring>

// Simulated RTC_NOINIT: each static state survives calls to boot().
static FlightPending::State rtc, damagedAck, damagedPending, orphanedPending, fresh;

int main() {
    using namespace FlightPending;
    boot(fresh, 0x101, 0);
    assert(validAck(fresh) && status(fresh) == Unavailable);
    assert(FlightRing::valid(fresh.current.header) && fresh.current.header.count == 0);
    FlightRing::append(fresh.current, 7, 1, 0, 0);
    boot(fresh, 0x405, 0x0706);
    assert(status(fresh) == Valid && fresh.pending.header.count == 1);
    assert(fresh.meta.summary == 0x405 && fresh.meta.rawReasons == 0x0706);

    boot(rtc, 0x101, 0);
    FlightRing::append(rtc.current, 17, 2, 3, 4);
    FlightRing::append(rtc.current, 25, 4, 5, 6);
    // Panic captures boot 1 and both ROM CPU reset reasons.
    boot(rtc, 0x405, 0x0706);
    assert(status(rtc) == Valid && rtc.pending.header.count == 2);
    assert(rtc.meta.summary == 0x405 && rtc.meta.rawReasons == 0x0706);
    const uint16_t crcFields[] = {uint16_t(rtc.meta.marker), uint16_t(rtc.meta.marker >> 16),
                                  rtc.meta.layout, rtc.meta.summary, rtc.meta.rawReasons,
                                  rtc.pending.header.crc};
    assert(rtc.meta.crc == FlightRing::checksum(crcFields, sizeof(crcFields)));
    auto changedHeader = rtc.pending.header;
    changedHeader.crc ^= 1;
    assert(check(rtc.meta, changedHeader) != rtc.meta.crc);
    changedHeader = rtc.pending.header;
    changedHeader.nextSequence ^= 1; // header CRC is the pending CRC input
    assert(check(rtc.meta, changedHeader) == rtc.meta.crc);
    FlightRing::append(rtc.current, 30, 1, 5, 0x1615);

    // Unattended crash then USB reset: pending-first, detecting reason persists.
    boot(rtc, 0x0c00, 0x1615);
    assert(status(rtc) == Valid && rtc.pending.header.count == 2);
    assert(rtc.meta.summary == 0x405 && rtc.meta.rawReasons == 0x0706);
    assert(FlightRing::at(rtc.pending, 0).ms == 17);
    assert(rtc.current.header.count == 0);
    // Replay/dump are read-only; writing bytes to a Stream cannot acknowledge.
    const auto savedBoot = rtc.pending.header.boot;
    auto beforeRead = rtc;
    unsigned replayed = 0, dumped = 0;
    visit(rtc, [&](unsigned, const FlightRing::Record &, bool valid) { assert(valid); ++replayed; });
    visit(rtc, [&](unsigned, const FlightRing::Record &, bool valid) { assert(valid); ++dumped; });
    assert(replayed == 2 && dumped == 2 && std::memcmp(&beforeRead, &rtc, sizeof(rtc)) == 0);
    assert(!acknowledge(rtc, 0) && !acknowledge(rtc, savedBoot + 1));
    assert(status(rtc) == Valid);
    auto partial = rtc;
    // Pending remains readable even if a slot is damaged.
    partial.pending.records[0].crc ^= 1;
    assert(status(partial) == Partial && !acknowledge(partial, savedBoot));
    assert(partial.pending.header.boot == savedBoot);
    assert(acknowledge(rtc, savedBoot));
    assert(validAck(rtc) && status(rtc) == Unavailable);
    assert(rtc.meta.crc == check(rtc.meta, uint16_t(0)));
    FlightRing::append(rtc.current, 42, 1, 0, 0);
    boot(rtc, 0x306, 0x0303);
    assert(status(rtc) == Valid && rtc.meta.summary == 0x306);
    assert(FlightRing::at(rtc.pending, 0).ms == 42);
    // TEST-only software restart intent is just an ordinary retained record;
    // it must not acknowledge or overwrite the pending capture on USB reset.
    FlightRing::append(rtc.current, 43, 3, 0xfe, 0);
    boot(rtc, 0x0c00, 0x1615);
    assert(status(rtc) == Valid && rtc.meta.summary == 0x306);
    assert(!acknowledge(rtc, uint16_t(rtc.pending.header.boot + 1)));

    // A bad pending marker must not be mistaken for an ACK, even with a
    // valid prior current ring. The pending bytes and reason stay untouched.
    damagedPending = rtc;
    damagedPending.meta.marker = 0; // old code mistook this for acknowledgment
    const auto pendingMeta = damagedPending.meta;
    const auto pendingHeader = damagedPending.pending.header;
    assert(status(damagedPending) == Corrupt);
    unsigned quarantined = 0;
    visit(damagedPending, [&](unsigned, const FlightRing::Record &, bool) { ++quarantined; });
    assert(quarantined == 0);
    boot(damagedPending, 0x0c00, 0x1615);
    assert(status(damagedPending) == Corrupt);
    assert(damagedPending.meta.marker == pendingMeta.marker &&
           damagedPending.meta.crc == pendingMeta.crc &&
           damagedPending.meta.summary == pendingMeta.summary);
    assert(damagedPending.pending.header.crc == pendingHeader.crc);
    assert(!acknowledge(damagedPending, damagedPending.pending.header.boot));
    assert(status(damagedPending) == Corrupt);

    // Even without a valid current ring, a pending header with a damaged
    // marker is evidence, not fresh RTC state eligible for an ACK.
    orphanedPending = rtc;
    orphanedPending.current.header.marker = 0;
    orphanedPending.meta.marker = 0;
    const auto orphanMeta = orphanedPending.meta;
    const auto orphanHeader = orphanedPending.pending.header;
    assert(FlightRing::valid(orphanHeader) && status(orphanedPending) == Corrupt);
    boot(orphanedPending, 0x0c00, 0x1615);
    assert(status(orphanedPending) == Corrupt);
    assert(orphanedPending.meta.marker == orphanMeta.marker &&
           orphanedPending.meta.crc == orphanMeta.crc &&
           orphanedPending.meta.summary == orphanMeta.summary);
    assert(orphanedPending.pending.header.crc == orphanHeader.crc);
    assert(FlightRing::valid(orphanedPending.current.header) &&
           orphanedPending.current.header.count == 0);

    // A damaged ACK (including its marker bytes) also cannot authorize a copy.
    damagedAck = rtc;
    assert(acknowledge(damagedAck, damagedAck.pending.header.boot));
    assert(validAck(damagedAck));
    FlightRing::append(damagedAck.current, 55, 1, 0, 0);
    damagedAck.meta.marker ^= 1;
    const auto ackMeta = damagedAck.meta;
    boot(damagedAck, 0x0c00, 0x1615);
    assert(status(damagedAck) == Corrupt);
    assert(damagedAck.meta.marker == ackMeta.marker && damagedAck.meta.crc == ackMeta.crc);
    assert(damagedAck.pending.header.crc == rtc.pending.header.crc);
    damagedAck.meta.marker = ackMagic;
    damagedAck.meta.crc ^= 1;
    assert(status(damagedAck) == Corrupt);
    boot(damagedAck, 0x101, 0);
    assert(status(damagedAck) == Corrupt);

    // Recognizable but invalid pending metadata is quarantined as well.
    ++rtc.meta.layout;
    assert(!acknowledge(rtc, rtc.pending.header.boot));
    assert(status(rtc) == Corrupt);
    boot(rtc, 0x0c00, 0x1615);
    assert(status(rtc) == Corrupt && rtc.meta.summary == 0x306);
    return 0;
}
