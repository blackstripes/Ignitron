#include "SparkReceiveAssembly.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#ifdef PANELAN_PRESET_TRACE
#include <string>
#include <vector>
static std::vector<SparkReceiveTraceEvent> events;
static void collect(const SparkReceiveTraceEvent &event) { events.push_back(event); }
static void expect(const char *name, const char *reason, uint8_t msg,
                   size_t expected, size_t received) {
    assert(events.size() == 1);
    const auto &e = events.front();
    assert(std::string(e.event) == name && std::string(e.reason) == reason);
    assert(e.message == msg && e.command == 3 && e.subcommand == 1);
    assert(e.expected == expected && e.received == received);
    events.clear();
}
#endif

using Frame = SparkReceiveAssembly::Frame;
using Frames = SparkReceiveAssembly::Frames;

// SparkMessage::splitDataToChunks / convertDataTo7Bit / buildChunkData wire
// shape (25 decoded bytes per 03 chunk). Do not mistake bytes of an
// unprefixed single-chunk preset for a three-byte multipart header.
static Frame wire(uint8_t seq, uint8_t cmd, uint8_t sub, const Frame &data) {
    Frame frame = {0xF0, 0x01, seq, 0, cmd, sub};
    for (size_t pos = 0; pos < data.size(); pos += 7) {
        uint8_t mask = 0;
        for (size_t i = pos; i < data.size() && i < pos + 7; ++i)
            if (data[i] & 0x80) mask |= uint8_t(1 << (i - pos));
        frame.push_back(mask);
        for (size_t i = pos; i < data.size() && i < pos + 7; ++i)
            frame.push_back(data[i] & 0x7F);
    }
    for (size_t i = 6; i < frame.size(); ++i) frame[3] ^= frame[i];
    frame.push_back(0xF7);
    return frame;
}

static Frame part(uint8_t seq, uint8_t cmd, uint8_t count, uint8_t index,
                  uint8_t size = 25) {
    Frame bytes = {count, index, size};
    bytes.insert(bytes.end(), size, uint8_t(0x80 + index));
    return wire(seq, cmd, 1, bytes);
}

static Frame presetPayload(const Frames &frames) {
    Frame payload;
    for (const auto &frame : frames) {
        auto decoded = SparkReceiveAssembly::decoded(frame);
        if (SparkReceiveAssembly::presetShape(frame, decoded) == 2)
            decoded.erase(decoded.begin(), decoded.begin() + 3);
        payload.insert(payload.end(), decoded.begin(), decoded.end());
    }
    return payload;
}

int main() {
    SparkReceiveFrames fragments;
    SparkReceiveAssembly assembler;
#ifdef PANELAN_PRESET_TRACE
    fragments.setTrace(collect);
    assembler.setTrace(collect);
#endif
    Frames output;
    auto feed = [&](const Frame &fragment) {
        bool published = false;
        for (const auto &frame : fragments.accept(fragment))
            published |= assembler.accept(frame, output);
        return published;
    };

    const auto one = wire(9, 3, 1, {0, 0, 0xD9, 3, 'a', 'b', 'c', 0xC3});
    const auto splitHeader = wire(0xF0, 3, 0x38, {1});
    assert(splitHeader[3] == 0x01);
    assert(fragments.accept(Frame(splitHeader.begin(), splitHeader.begin() + 2)).empty());
    assert(fragments.accept(Frame(splitHeader.begin() + 2, splitHeader.end())) ==
           Frames({splitHeader}));
    // Genuine new frame start while the preceding frame is incomplete.
    assert(fragments.accept(Frame(one.begin(), one.begin() + 5)).empty());
    assert(fragments.accept(splitHeader) == Frames({splitHeader}));
    for (size_t cut = 1; cut < one.size(); ++cut) {
        fragments.reset();
        assert(fragments.accept(Frame(one.begin(), one.begin() + cut)).empty());
        assert(fragments.accept(Frame(one.begin() + cut, one.end())) == Frames({one}));
    }
    assert(!feed({0xF0})); // exactly between the two start bytes
    assert(feed(Frame(one.begin() + 1, one.end())));
    assert(output.size() == 1 && presetPayload(output) ==
           Frame({0, 0, 0xD9, 3, 'a', 'b', 'c', 0xC3}));
    fragments.reset(); assembler.reset();
    assert(!feed(Frame(one.begin(), one.begin() + 5)));
    assert(feed(Frame(one.begin() + 5, one.end())));
    assert(presetPayload(output).size() == 8);

    assert(!feed(part(16, 3, 3, 0)));
    const auto singleSolo = wire(17, 3, 1, {0, 2, 0xD9, 3, 's', 'o', 'l', 0xC3});
    assert(feed(singleSolo)); // unprefixed single-chunk preset supersedes stale partial
    assert(output.size() == 1 && output[0] == singleSolo);
    assert(!feed(part(16, 3, 3, 1)));
    assert(!feed(part(16, 3, 3, 2, 7))); // old assembly cannot resume

    for (int i = 0; i < 16; ++i) assert(!feed(part(16, 3, 17, i)));
    const auto other = wire(42, 3, 0x38, {0, 3});
    assert(feed(other)); // interleave unrelated single-frame notification
    assert(output.size() == 1 && output[0] == other);
    assert(feed(part(16, 3, 17, 16, 7)));
    assert(output.size() == 17 && presetPayload(output).size() == 407);

    // Two complete frames in one BLE notification; a frame completion followed
    // by the next frame's F0 (and then its remainder in another notification).
    Frame joined = other;
    joined.insert(joined.end(), one.begin(), one.end());
    assert(fragments.accept(joined) == Frames({other, one}));
    Frame half = other;
    half.push_back(0xF0);
    assert(fragments.accept(half) == Frames({other}));
    assert(fragments.accept(Frame(one.begin() + 1, one.end())) == Frames({one}));

    assert(!feed(part(16, 3, 3, 0)));
    assert(!feed(part(16, 3, 3, 0))); // duplicate start
    assert(!feed(part(16, 3, 3, 1))); // no start
    assert(!feed(part(16, 3, 3, 0)));
    assert(!feed(part(16, 3, 3, 1)));
    assert(!feed(part(16, 3, 3, 1))); // duplicate continuation
    assert(!feed(part(16, 3, 3, 2, 7)));
    assert(!feed(part(16, 3, 3, 0)));
    assert(!feed(part(16, 3, 3, 2, 7))); // gap
    assert(!feed(part(16, 3, 3, 0)));
    assert(!feed(part(16, 1, 3, 1, 128))); // different command
    assert(!feed(part(16, 3, 3, 2, 7)));
    assert(!feed(part(16, 3, 3, 0)));
    assert(!feed(part(16, 3, 4, 1))); // different count
    assert(!feed(part(16, 3, 3, 0)));
    assembler.reset(); fragments.reset(); // link / parser reset
    assert(!feed(part(16, 3, 3, 1)));

    // Captured retry: seq16 lacks part16; an anomalous 1/17 tail arrives,
    // then seq17 starts at part1. Neither incomplete response may publish.
    for (int i = 0; i < 16; ++i) assert(!feed(part(16, 3, 17, i)));
    assert(!feed(wire(16, 3, 1, {1, 17, 25, 0x80})));
    for (int i = 1; i < 17; ++i)
        assert(!feed(part(17, 3, 17, i, i == 16 ? 7 : 25)));
    assert(output.empty()); // no mixed 822-byte payload reaches parser
    for (int i = 0; i < 16; ++i) assert(!feed(part(17, 3, 17, i)));
    assert(feed(part(17, 3, 17, 16, 7)));
    assert(output.size() == 17 && presetPayload(output).size() == 407);
    for (int i = 0; i < 17; ++i) assert(output[i][2] == 17);
#ifdef PANELAN_PRESET_TRACE
    events.clear();
    auto bad = one;
    bad[3] ^= 1;
    assert(fragments.accept(bad).empty());
    expect("frame_discard", "checksum", 9, 0, bad.size());
    assert(!assembler.accept(part(30, 3, 3, 0), output));
    assert(events.size() == 2);
    assert(std::string(events[0].event) == "multipart_start");
    assert(std::string(events[1].event) == "multipart_progress");
    assert(events[0].message == 30 && events[0].expected == 3 && events[0].received == 0);
    assert(events[1].received == 1);
    events.clear();
    assert(!assembler.accept(part(30, 3, 3, 2, 7), output));
    expect("multipart_discard", "out_of_order", 30, 3, 1);
    assert(!assembler.accept(part(31, 3, 2, 0), output));
    events.clear();
    assembler.reset();
    expect("multipart_discard", "reset", 31, 2, 1);
    assert(!assembler.accept(part(32, 3, 2, 0), output));
    events.clear();
    assert(assembler.accept(part(32, 3, 2, 1, 7), output));
    expect("multipart_complete", "none", 32, 2, 2);
    assert(!assembler.accept(part(33, 3, 2, 0), output));
    events.clear();
    assert(!assembler.accept(part(33, 3, 2, 0), output));
    expect("multipart_discard", "duplicate_start", 33, 2, 1);
    fragments.accept(Frame(one.begin(), one.begin() + 6));
    events.clear();
    fragments.reset();
    expect("frame_discard", "reset", 9, 0, 6);

    // A bad candidate before a good start is discarded even when recovery succeeds.
    Frame invalidHeader(one.begin(), one.begin() + 6);
    invalidHeader[4] |= 0x80;
    invalidHeader.insert(invalidHeader.end(), one.begin(), one.end());
    assert(fragments.accept(invalidHeader) == Frames({one}));
    assert(events.size() == 1 && std::string(events[0].event) == "frame_discard" &&
           std::string(events[0].reason) == "invalid_wire_resync" &&
           events[0].message == 9 && events[0].command == 0x83 &&
           events[0].received == invalidHeader.size());
    events.clear();
    Frame invalidData(one.begin(), one.begin() + 6);
    invalidData.push_back(0x80);
    invalidData.insert(invalidData.end(), one.begin(), one.end());
    assert(fragments.accept(invalidData) == Frames({one}));
    expect("frame_discard", "invalid_wire_resync", 9, 0, invalidData.size());

    // Rejection belongs to the incoming frame; an existing assembly has its
    // own, separate discard event, and neither is required for the other.
    auto malformed = one;
    malformed.back() = 0;
    assert(!assembler.accept(malformed, output));
    expect("incoming_reject", "invalid_frame", 9, 0, 0);
    const auto badShape = wire(41, 3, 1, {3, 0, 25, 0x80});
    assert(!assembler.accept(badShape, output));
    expect("incoming_reject", "invalid_shape", 41, 0, 0);
    assert(!assembler.accept(part(40, 3, 2, 0), output));
    events.clear();
    assert(!assembler.accept(badShape, output));
    assert(events.size() == 2);
    assert(std::string(events[0].event) == "incoming_reject" &&
           std::string(events[0].reason) == "invalid_shape" && events[0].message == 41);
    assert(std::string(events[1].event) == "multipart_discard" &&
           std::string(events[1].reason) == "invalid_shape" && events[1].message == 40);
    events.clear();

    assert(fragments.accept({0xF0, 0x02}).empty());
    assert(events.size() == 1);
    assert(std::string(events[0].event) == "frame_discard" &&
           std::string(events[0].reason) == "malformed_start");
    events.clear();
    assert(fragments.accept({0xF0, 0xF0}).empty());
    assert(events.size() == 1 && std::string(events[0].reason) == "malformed_start");
    events.clear();
    assert(fragments.accept(Frame(one.begin() + 1, one.end())) == Frames({one}));
    assert(events.empty());
#endif
    std::cout << "Spark receive framing: PASS\n";
}
