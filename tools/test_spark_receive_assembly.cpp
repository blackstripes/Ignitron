#include "SparkReceiveAssembly.h"

#include <algorithm>
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

static Frames acceptWithIngressId(SparkReceiveFrames &reader, const Frame &fragment, uint32_t id) {
#ifdef PANELAN_PRESET_TRACE
    return reader.accept(fragment, id);
#else
    (void)id;
    return reader.accept(fragment);
#endif
}

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

    // F7 is legal in the sequence-number position. Before the minimum-frame
    // length guard, the reader terminated this valid frame at byte three.
    const auto sequenceF7 = wire(0xF7, 3, 1, {0, 0, 0xD9, 3, 'f', '7', 0xC3});
    assert(sequenceF7[2] == 0xF7 && sequenceF7.back() == 0xF7);
#ifdef PANELAN_PRESET_TRACE
    events.clear();
#endif
    const Frames sequenceF7Result = acceptWithIngressId(fragments, sequenceF7, 77);
    if (sequenceF7Result != Frames({sequenceF7})) {
#ifdef PANELAN_PRESET_TRACE
        for (const auto &event : events)
            std::cerr << "observed " << event.event << " reason=" << event.reason
                      << " received=" << event.received << " first_id=" << event.firstIngressId
                      << " last_id=" << event.lastIngressId << '\n';
#endif
        std::cerr << "sequence 0xF7 did not produce its complete Spark frame\n";
    }
    assert(sequenceF7Result == Frames({sequenceF7}));
#ifdef PANELAN_PRESET_TRACE
    assert(events.size() == 1 && std::string(events[0].event) == "frame_span_complete");
    assert(events[0].firstIngressId == 77 && events[0].lastIngressId == 77 &&
           events[0].partialBytes == sequenceF7.size() && events[0].validStart &&
           events[0].headerValid && events[0].terminatorSeen && events[0].frameSpanExact);
    events.clear();
#endif

    // A true short F0 01 F7 sequence is not a complete frame. It remains
    // partial until reset; it must never be published as a wire message.
    SparkReceiveFrames shortReader;
#ifdef PANELAN_PRESET_TRACE
    shortReader.setTrace(collect);
    events.clear();
#endif
    assert(acceptWithIngressId(shortReader, {0xF0, 0x01, 0xF7}, 78).empty());
#ifdef PANELAN_PRESET_TRACE
    assert(events.empty());
#endif
    shortReader.reset();
#ifdef PANELAN_PRESET_TRACE
    assert(events.size() == 1 && std::string(events[0].event) == "frame_discard" &&
           std::string(events[0].reason) == "reset" && events[0].received == 3 &&
           events[0].firstIngressId == 78 && events[0].lastIngressId == 78 &&
           events[0].validStart && !events[0].headerValid && events[0].terminatorSeen);
    events.clear();
#endif

    // The same sequence-position delimiter must work across every possible
    // notification split, including a fragment ending immediately after F7.
    for (size_t cut = 1; cut < sequenceF7.size(); ++cut) {
        SparkReceiveFrames splitReader;
#ifdef PANELAN_PRESET_TRACE
        splitReader.setTrace(collect);
        events.clear();
#endif
        assert(acceptWithIngressId(splitReader,
                                   Frame(sequenceF7.begin(), sequenceF7.begin() + cut), 79).empty());
        assert(acceptWithIngressId(splitReader,
                                   Frame(sequenceF7.begin() + cut, sequenceF7.end()), 80) ==
               Frames({sequenceF7}));
#ifdef PANELAN_PRESET_TRACE
        assert(events.size() == 1 && std::string(events[0].event) == "frame_span_complete" &&
               events[0].firstIngressId == 79 && events[0].lastIngressId == 80 &&
               events[0].partialBytes == sequenceF7.size());
        events.clear();
#endif
    }

    // Preserve ordinary framing, sequence 0xF0, coalesced delivery, and the
    // normal message-number wrap from 0xFF to 0x01.
    const auto sequenceF0 = wire(0xF0, 3, 1, {0, 0, 0xD9, 3, 'f', '0', 0xC3});
    assert(fragments.accept(one) == Frames({one}));
    assert(fragments.accept(sequenceF0) == Frames({sequenceF0}));
    assert(fragments.accept(wire(0xFF, 3, 0x38, {1})) ==
           Frames({wire(0xFF, 3, 0x38, {1})}));
    assert(fragments.accept(wire(0x01, 3, 0x38, {2})) ==
           Frames({wire(0x01, 3, 0x38, {2})}));
    Frame coalescedF7 = wire(0xFE, 3, 0x38, {3});
    coalescedF7.insert(coalescedF7.end(), sequenceF7.begin(), sequenceF7.end());
    assert(fragments.accept(coalescedF7) == Frames({wire(0xFE, 3, 0x38, {3}), sequenceF7}));

    // A complete 17-part full-preset response with message number 0xF7 must
    // survive framing and reach the multipart assembler as one response.
    SparkReceiveFrames multipartReader;
    SparkReceiveAssembly multipartAssembler;
    Frames multipartOutput;
#ifdef PANELAN_PRESET_TRACE
    multipartReader.setTrace(collect);
    multipartAssembler.setTrace(collect);
#endif
    bool multipartPublished = false;
    for (uint8_t index = 0; index < 17; ++index) {
        const Frame wirePart = part(0xF7, 3, 17, index, index == 16 ? 7 : 25);
        for (const auto &completeFrame : acceptWithIngressId(multipartReader, wirePart, 100 + index))
            multipartPublished |= multipartAssembler.accept(completeFrame, multipartOutput);
    }
    assert(multipartPublished && multipartOutput.size() == 17);
    for (const auto &completeFrame : multipartOutput) assert(completeFrame[2] == 0xF7);
#ifdef PANELAN_PRESET_TRACE
    assert(std::any_of(events.begin(), events.end(), [](const SparkReceiveTraceEvent &event) {
        return std::string(event.event) == "multipart_complete" && event.message == 0xF7 &&
               event.expected == 17 && event.received == 17;
    }));
    events.clear();
#endif

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
    assert(fragments.accept(bad, 70).empty());
    expect("frame_discard", "checksum", 9, 0, bad.size());
    // The complete event identifies the actual candidate rather than the
    // aggregate buffer, and retains both IDs across notification boundaries.
    assert(fragments.accept(Frame(one.begin(), one.begin() + 6), 71).empty());
    assert(fragments.accept(Frame(one.begin() + 6, one.end()), 72) == Frames({one}));
    assert(events.size() == 1);
    assert(std::string(events[0].event) == "frame_span_complete" && events[0].message == 9);
    assert(events[0].firstIngressId == 71 && events[0].lastIngressId == 72);
    assert(events[0].partialBytes == one.size() && events[0].received == one.size());
    assert(events[0].validStart && events[0].headerValid && events[0].terminatorSeen &&
           events[0].frameSpanExact);
    events.clear();
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
    fragments.accept(Frame(one.begin(), one.begin() + 6), 73);
    events.clear();
    fragments.reset();
    expect("frame_discard", "reset", 9, 0, 6);
    assert(fragments.accept(Frame(one.begin(), one.begin() + 1), 74).empty());
    assert(fragments.accept(Frame(one.begin() + 1, one.begin() + 5), 75).empty());
    events.clear();
    fragments.reset();
    assert(events.size() == 1 && events[0].firstIngressId == 74 &&
           events[0].lastIngressId == 75 && events[0].partialBytes == 5 &&
           events[0].validStart && !events[0].headerValid && !events[0].terminatorSeen &&
           !events[0].frameSpanExact);
    events.clear();

    // A bad candidate before a good start is discarded even when recovery succeeds.
    Frame invalidHeader(one.begin(), one.begin() + 6);
    invalidHeader[4] |= 0x80;
    invalidHeader.insert(invalidHeader.end(), one.begin(), one.end());
    assert(fragments.accept(Frame(invalidHeader.begin(), invalidHeader.begin() + 6), 76).empty());
    assert(fragments.accept(Frame(invalidHeader.begin() + 6, invalidHeader.end()), 77) == Frames({one}));
    assert(events.size() == 2 && std::string(events[0].event) == "frame_discard" &&
           std::string(events[0].reason) == "invalid_wire_resync" &&
           !events[0].headerValid && events[0].validStart && !events[0].frameSpanExact &&
           events[0].firstIngressId == 76 && events[0].lastIngressId == 77 &&
           events[0].received == invalidHeader.size());
    assert(std::string(events[1].event) == "frame_span_complete" && events[1].frameSpanExact &&
           events[1].firstIngressId == 77 && events[1].lastIngressId == 77 &&
           events[1].partialBytes == one.size() && events[1].message == 9);
    events.clear();
    Frame invalidData(one.begin(), one.begin() + 6);
    invalidData.push_back(0x80);
    invalidData.insert(invalidData.end(), one.begin(), one.end());
    assert(fragments.accept(invalidData) == Frames({one}));
    assert(events.size() == 2 && std::string(events[0].reason) == "invalid_wire_resync" &&
           events[0].received == invalidData.size() && !events[0].frameSpanExact &&
            std::string(events[1].event) == "frame_span_complete");
    events.clear();
    assert(fragments.accept(Frame(bad.begin(), bad.begin() + 6), 78).empty());
    assert(fragments.accept(Frame(bad.begin() + 6, bad.end()), 81).empty());
    assert(events.size() == 1 && std::string(events[0].reason) == "checksum" &&
           events[0].firstIngressId == 78 && events[0].lastIngressId == 81 &&
           events[0].partialBytes == bad.size() && events[0].terminatorSeen &&
           !events[0].frameSpanExact);
    events.clear();
    // Overflow retains only the most recent start. Its new ingress range
    // must not include the bytes erased during recovery.
    Frame overflow = {0xF0, 0x01};
    overflow.insert(overflow.end(), 1017, 0x02);
    overflow.insert(overflow.end(), one.begin(), one.begin() + 6);
    assert(fragments.accept(overflow, 79).empty());
    assert(events.size() == 1 && std::string(events[0].reason) == "overflow_resync" &&
           events[0].partialBytes == 1019 && !events[0].frameSpanExact);
    events.clear();
    assert(fragments.accept(Frame(one.begin() + 6, one.end()), 80) == Frames({one}));
    assert(events.size() == 1 && events[0].firstIngressId == 79 &&
           events[0].lastIngressId == 80 && events[0].partialBytes == one.size());
    events.clear();

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
    assert(events.size() == 1 && std::string(events[0].event) == "frame_span_complete" &&
           events[0].firstIngressId == 0 && events[0].lastIngressId == 0);
#endif
    std::cout << "Spark receive framing: PASS\n";
}
