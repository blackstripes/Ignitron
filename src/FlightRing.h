#pragma once
#include <stdint.h>
#include <stddef.h>

// Plain-data RTC layout; no constructors or pointers in retained memory.
namespace FlightRing {
constexpr uint32_t magic = 0x464C5231u; // FLR1
constexpr uint16_t version = 2;
constexpr unsigned capacity = 64;
struct Record {
    uint32_t sequence, ms;
    uint16_t value, boot;
    uint8_t event, detail;
    uint16_t crc;
};
struct Header {
    uint32_t marker, nextSequence;
    uint16_t layout, boot, count, write, overwritten, crc;
};
struct Store { Header header; Record records[capacity]; };
static_assert(sizeof(Record) == 16 && sizeof(Store) == 1044, "RTC flight layout changed");
static_assert(offsetof(Header, crc) == 18, "header CRC must cover overwritten count");
inline uint16_t checksum(const void *data, size_t length) {
    const auto *bytes = static_cast<const uint8_t *>(data);
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc & 1) ? (crc >> 1) ^ 0xa001 : crc >> 1;
    }
    return crc;
}
inline bool valid(const Header &h) {
    return h.marker == magic && h.layout == version && h.boot && h.nextSequence &&
           h.count <= capacity && h.write < capacity &&
           (h.count == capacity || h.write == h.count) &&
           h.crc == checksum(&h, offsetof(Header, crc));
}
inline bool valid(const Record &r) {
    return r.sequence && r.boot && r.crc == checksum(&r, offsetof(Record, crc));
}
inline void initialize(Store &s, uint16_t boot = 1, uint32_t sequence = 1) {
    s.header = {magic, sequence ? sequence : 1, version, boot ? boot : uint16_t(1), 0, 0, 0, 0};
    s.header.crc = checksum(&s.header, offsetof(Header, crc));
}
inline void append(Store &s, uint32_t ms, uint8_t event, uint8_t detail, uint16_t value) {
    Header &h = s.header;
    Record &r = s.records[h.write];
    r = {h.nextSequence, ms, value, h.boot, event, detail, 0};
    r.crc = checksum(&r, offsetof(Record, crc));
    ++h.nextSequence;
    if (!h.nextSequence) h.nextSequence = 1;
    h.write = (h.write + 1) % capacity;
    if (h.count < capacity) ++h.count;
    else if (h.overwritten != UINT16_MAX) ++h.overwritten;
    h.crc = checksum(&h, offsetof(Header, crc));
}
inline Record at(const Store &s, unsigned index) {
    return s.records[(s.header.write + capacity - s.header.count + index) % capacity];
}
// Expected sequence for a slot in a captured header. Sequence zero is skipped.
inline uint32_t expectedSequence(const Header &h, unsigned index) {
    const uint32_t distance = h.count - index;
    return h.nextSequence > distance ? h.nextSequence - distance :
        UINT32_MAX - (distance - h.nextSequence);
}
inline bool matchesSnapshot(const Header &h, const Record &r, unsigned index) {
    return valid(r) && r.boot == h.boot && r.sequence == expectedSequence(h, index);
}
}
