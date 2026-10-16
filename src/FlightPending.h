#pragma once
#include "FlightRing.h"

// RTC-only state machine, also compiled by the host tests. Never allocate a Store
// on the stack: both stores live in RTC_NOINIT on device.
namespace FlightPending {
constexpr uint32_t magic = 0x46504e31u; // FPN1
constexpr uint32_t ackMagic = 0x46414331u; // FAC1
constexpr uint16_t version = 2;
enum Status : uint8_t { Unavailable, Valid, Partial, Corrupt };
struct Metadata {
    uint32_t marker;
    uint16_t layout, summary, rawReasons, crc;
};
static_assert(sizeof(Metadata) == 12 && offsetof(Metadata, marker) == 0 &&
              offsetof(Metadata, layout) == 4 && offsetof(Metadata, summary) == 6 &&
              offsetof(Metadata, rawReasons) == 8 && offsetof(Metadata, crc) == 10,
              "RTC pending metadata layout changed");
static_assert(offsetof(FlightRing::Header, crc) == 18 && sizeof(FlightRing::Header) == 20,
              "RTC pending header layout changed");
struct State {
    FlightRing::Store current, pending;
    Metadata meta;
};
static_assert(sizeof(State) == 2100, "RTC pending layout changed");

inline uint16_t check(const Metadata &m, uint16_t headerCrc) {
    // Include marker bytes in both states; never include object padding. ACK
    // uses a fixed zero instead of a potentially stale pending header CRC.
    const uint16_t fields[] = {uint16_t(m.marker), uint16_t(m.marker >> 16),
                               m.layout, m.summary, m.rawReasons, headerCrc};
    static_assert(sizeof(fields) == 12, "pending CRC input layout changed");
    return FlightRing::checksum(fields, sizeof(fields));
}
inline uint16_t check(const Metadata &m, const FlightRing::Header &h) {
    return check(m, h.crc);
}
inline bool validAck(const State &s) {
    return s.meta.marker == ackMagic && s.meta.layout == version &&
           s.meta.summary == 0 && s.meta.rawReasons == 0 &&
           s.meta.crc == check(s.meta, uint16_t(0));
}
inline void setAck(State &s) {
    s.meta.marker = 0; // invalid until the CRC and all fields are ready
    s.meta.layout = version;
    s.meta.summary = 0;
    s.meta.rawReasons = 0;
    Metadata committed = s.meta;
    committed.marker = ackMagic;
    s.meta.crc = check(committed, uint16_t(0));
    s.meta.marker = ackMagic;
}
inline bool validMeta(const State &s) {
    return s.meta.marker == magic && s.meta.layout == version &&
           FlightRing::valid(s.pending.header) &&
           s.meta.crc == check(s.meta, s.pending.header);
}
inline Status status(const State &s) {
    if (validAck(s)) return Unavailable;
    if (!validMeta(s)) return Corrupt;
    for (unsigned i = 0; i < s.pending.header.count; ++i)
        if (!FlightRing::matchesSnapshot(s.pending.header, FlightRing::at(s.pending, i), i))
            return Partial;
    return Valid;
}
inline void boot(State &s, uint16_t summary, uint16_t rawReasons) {
    const bool prior = FlightRing::valid(s.current.header);
    // Only a validated ACK permits replacement. Unknown or torn metadata with
    // a valid current ring must remain quarantined rather than losing evidence.
    if (validAck(s) && prior) {
        s.pending = s.current;
        s.meta.marker = 0; // invalid during copy/metadata construction
        s.meta.layout = version;
        s.meta.summary = summary;
        s.meta.rawReasons = rawReasons;
        Metadata committed = s.meta;
        committed.marker = magic;
        s.meta.crc = check(committed, s.pending.header);
        s.meta.marker = magic; // commit last
    } else if (!prior && !FlightRing::valid(s.pending.header) &&
               s.meta.marker != magic && !validAck(s)) {
        // Truly fresh RTC state has no valid ring or recognizable pending.
        // A valid pending header remains evidence even with a torn marker.
        setAck(s);
    }
    const uint16_t nextBoot = prior ? uint16_t(s.current.header.boot + 1) : 1;
    const uint32_t nextSequence = prior ? s.current.header.nextSequence : 1;
    FlightRing::initialize(s.current, nextBoot, nextSequence);
}
inline void acknowledge(State &s) {
    // Keep the snapshot bytes; only a complete ACK permits the next latch.
    if (validMeta(s)) setAck(s);
}
inline bool acknowledgeAfterOutput(State &s, bool allWritesAccepted,
                                  bool streamWriteError, bool serialStillConnected) {
    if (!allWritesAccepted || streamWriteError || !serialStillConnected || !validMeta(s)) return false;
    acknowledge(s);
    return validAck(s);
}
}
