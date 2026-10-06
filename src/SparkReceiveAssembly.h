#ifndef SPARK_RECEIVE_ASSEMBLY_H
#define SPARK_RECEIVE_ASSEMBLY_H

#include <cstdint>
#include <vector>

#ifdef PANELAN_PRESET_TRACE
// Metadata only: never retain or pass frame data to a trace sink.
struct SparkReceiveTraceEvent {
    const char *event;
    const char *reason;
    uint8_t message, command, subcommand;
    size_t expected, received;
    // Frame-reader only. IDs bound the retained byte range, not necessarily a
    // single wire frame (resync may have accumulated multiple candidates).
    uint32_t firstIngressId = 0, lastIngressId = 0;
    size_t partialBytes = 0;
    bool validStart = false, headerValid = false, terminatorSeen = false;
    bool frameSpanExact = false;

    SparkReceiveTraceEvent(const char *eventName, const char *eventReason,
                           uint8_t eventMessage, uint8_t eventCommand,
                           uint8_t eventSubcommand, size_t eventExpected,
                           size_t eventReceived)
        : event(eventName), reason(eventReason), message(eventMessage),
          command(eventCommand), subcommand(eventSubcommand),
          expected(eventExpected), received(eventReceived) {}
};
using SparkReceiveTraceSink = void (*)(const SparkReceiveTraceEvent &);
#endif

// Join BLE fragments into complete wire frames. Do not interpret a fragment
// boundary as a frame boundary: F0 01 may also be the sequence/checksum pair
// immediately following a split start marker. SparkMessage::buildChunkData
// XORs the seven-bit data bytes into the frame checksum.
class SparkReceiveFrames {
public:
    using Frame = std::vector<uint8_t>;
    using Frames = std::vector<Frame>;

#ifdef PANELAN_PRESET_TRACE
    void setTrace(SparkReceiveTraceSink sink) { trace_ = sink; }
#endif
    void reset() {
#ifdef PANELAN_PRESET_TRACE
        if (!partial_.empty()) trace("frame_discard", "reset", 0, partial_.size());
        ingress_.clear();
#endif
        partial_.clear();
    }
    Frames accept(const Frame &fragment
#ifdef PANELAN_PRESET_TRACE
                  , uint32_t ingressId = 0
#endif
                  ) {
        Frames ready;
        for (uint8_t value : fragment) {
            if (partial_.empty() && value != 0xF0) continue;
            if (partial_.size() == 1 && value != 0x01) {
#ifdef PANELAN_PRESET_TRACE
                trace("frame_discard", "malformed_start", 0, partial_.size());
                ingress_.clear();
#endif
                partial_.clear();
                if (value != 0xF0) continue;
            }
            partial_.push_back(value);
#ifdef PANELAN_PRESET_TRACE
            ingress_.push_back(ingressId);
#endif
            if (value == 0xF7) {
                // A stale incomplete frame may precede a fresh frame in this
                // buffer. Prefer the earliest valid candidate; only seek a
                // later F0 01 if the earlier one fails its wire checksum.
#ifdef PANELAN_PRESET_TRACE
                bool rejectedChecksum = false;
                bool rejectedInvalidWire = false;
                bool found = false;
                size_t acceptedPos = partial_.size();
#endif
                for (size_t pos = 0; pos + 6 < partial_.size(); ++pos) {
                    if (partial_[pos] != 0xF0 || partial_[pos + 1] != 0x01) continue;
                    if ((partial_[pos + 4] & 0x80) || (partial_[pos + 5] & 0x80)) {
#ifdef PANELAN_PRESET_TRACE
                        rejectedInvalidWire = true;
#endif
                        continue;
                    }
                    uint8_t sum = 0;
                    bool sevenBit = true;
                    for (size_t i = pos + 6; i + 1 < partial_.size(); ++i) {
                        if (partial_[i] & 0x80) sevenBit = false;
                        sum ^= partial_[i];
                    }
                    if (!sevenBit || sum != partial_[pos + 3]) {
#ifdef PANELAN_PRESET_TRACE
                        if (sevenBit) rejectedChecksum = true;
                        else rejectedInvalidWire = true;
#endif
                        continue;
                    }
                    ready.emplace_back(partial_.begin() + pos, partial_.end());
#ifdef PANELAN_PRESET_TRACE
                    found = true;
                    acceptedPos = pos;
#endif
                    break;
                }
#ifdef PANELAN_PRESET_TRACE
                // A rejected candidate was checked against the aggregate up
                // to F7. It can overlap the recovered frame: do not label its
                // ingress range as an exact rejected frame span.
                if (rejectedChecksum) trace("frame_discard", found ? "checksum_resync" : "checksum",
                                            0, partial_.size());
                if (rejectedInvalidWire || (!found && !rejectedChecksum))
                    trace("frame_discard", found ? "invalid_wire_resync" : "invalid_wire",
                          0, partial_.size());
                if (found) trace("frame_span_complete", "none", acceptedPos, partial_.size(), true);
                ingress_.clear();
#endif
                partial_.clear();
            } else if (partial_.size() > 1024) {
                // Bounded recovery from a lost F7; keep the most recent start
                // rather than growing indefinitely on a damaged BLE stream.
                size_t start = partial_.size();
                for (size_t i = 1; i + 1 < partial_.size(); ++i)
                    if (partial_[i] == 0xF0 && partial_[i + 1] == 0x01) start = i;
                if (start < partial_.size()) {
#ifdef PANELAN_PRESET_TRACE
                    trace("frame_discard", "overflow_resync", 0, start);
                    ingress_.erase(ingress_.begin(), ingress_.begin() + start);
#endif
                    partial_.erase(partial_.begin(), partial_.begin() + start);
                } else {
#ifdef PANELAN_PRESET_TRACE
                    trace("frame_discard", "overflow", 0, partial_.size());
                    ingress_.clear();
#endif
                    partial_.clear();
                }
            }
        }
        return ready;
    }

private:
    Frame partial_;
#ifdef PANELAN_PRESET_TRACE
    SparkReceiveTraceSink trace_ = nullptr;
    // Parallel only in trace builds, bounded by the same 1024-byte recovery
    // limit as partial_. No payload data is passed to the sink.
    std::vector<uint32_t> ingress_;
    void trace(const char *event, const char *reason, size_t begin, size_t end,
               bool exact = false) const {
        if (!trace_) return;
        const bool start = end - begin >= 2 && partial_[begin] == 0xF0 && partial_[begin + 1] == 0x01;
        const bool header = start && end - begin >= 6 &&
                            !(partial_[begin + 4] & 0x80) && !(partial_[begin + 5] & 0x80);
        SparkReceiveTraceEvent info{event, reason, header ? partial_[begin + 2] : uint8_t(0),
                                    header ? partial_[begin + 4] : uint8_t(0),
                                    header ? partial_[begin + 5] : uint8_t(0), 0, end - begin};
        info.firstIngressId = ingress_[begin];
        info.lastIngressId = ingress_[end - 1];
        info.partialBytes = end - begin;
        info.validStart = start;
        info.headerValid = header;
        info.terminatorSeen = partial_[end - 1] == 0xF7;
        info.frameSpanExact = exact;
        trace_(info);
    }
#endif
};

// Accepts complete F0 01 ... F7 frames. The encoder adds a [count, part,
// chunkLength] prefix only when count > 1. Unrelated single-frame notifications
// do not disturb a pending preset.
class SparkReceiveAssembly {
public:
    using Frame = std::vector<uint8_t>;
    using Frames = std::vector<Frame>;

#ifdef PANELAN_PRESET_TRACE
    void setTrace(SparkReceiveTraceSink sink) { trace_ = sink; }
#endif
    void reset() { discard("reset"); }

    // Decode the same 7-to-8-bit layout as SparkMessage::convertDataTo7Bit.
    static Frame decoded(const Frame &frame) {
        Frame result;
        if (frame.size() < 7) return result;
        for (size_t pos = 6; pos + 1 < frame.size();) {
            uint8_t mask = frame[pos++];
            for (int bit = 0; bit < 7 && pos + 1 < frame.size(); ++bit)
                result.push_back(frame[pos++] | ((mask & (1 << bit)) ? 0x80 : 0));
        }
        return result;
    }

    // 0 = invalid, 1 = unprefixed, 2 = multipart.
    static int presetShape(const Frame &frame, const Frame &data) {
        if (data.size() >= 3 && data[0] > 1) {
            const size_t maxChunk = frame[4] == 0x01 ? 0x80 : 0x19;
            const uint8_t count = data[0], part = data[1], length = data[2];
            if (part < count && length > 0 && length <= maxChunk &&
                data.size() == size_t(length) + 3 &&
                (part == count - 1 || length == maxChunk)) return 2;
            return 0;
        }
        // Unprefixed preset: address and serialized first string. This avoids
        // treating a malformed multipart tail as a standalone preset.
        return data.size() >= 3 && data[0] <= 1 && data[2] >= 0xA0 ? 1 : 0;
    }

    bool accept(const Frame &frame, Frames &complete) {
        complete.clear();
        if (frame.size() < 7 || frame[0] != 0xF0 || frame[1] != 0x01 ||
            frame.back() != 0xF7) {
#ifdef PANELAN_PRESET_TRACE
            reject(frame, "invalid_frame");
#endif
            discard("invalid_frame");
            return false;
        }
        const bool preset = (frame[4] == 0x01 || frame[4] == 0x03) && frame[5] == 0x01;
        if (!preset) {
            complete.push_back(frame);
            return true;
        }
        const Frame data = decoded(frame);
        const int shape = presetShape(frame, data);
        if (shape == 0) {
#ifdef PANELAN_PRESET_TRACE
            reject(frame, "invalid_shape");
#endif
            discard("invalid_shape");
            return false;
        }
        if (shape == 1) {
            // An unprefixed single-frame preset is a new logical message too;
            // never allow an older multipart response to resume after it.
            discard("single_preset");
            complete.push_back(frame);
            return true;
        }
        const uint8_t count = data[0], part = data[1];
        // A new start supersedes a stale incomplete response. A non-start
        // with a different identity cannot borrow bytes from the old one.
        if (part == 0 && !pending_.empty()) {
            const bool duplicate = frame[2] == sequence_ && frame[4] == command_ &&
                                   frame[5] == subcommand_ && count == count_;
            discard(duplicate ? "duplicate_start" : "new_start");
            if (duplicate) return false;
        }
        if (pending_.empty()) {
            if (part != 0) {
#ifdef PANELAN_PRESET_TRACE
                emit("multipart_discard", "missing_start", frame[2], frame[4], frame[5], count, 0);
#endif
                return false;
            }
            sequence_ = frame[2];
            command_ = frame[4];
            subcommand_ = frame[5];
            count_ = count;
#ifdef PANELAN_PRESET_TRACE
            emit("multipart_start", "none", sequence_, command_, subcommand_, count_, 0);
#endif
        } else if (frame[2] != sequence_ || frame[4] != command_ ||
                   frame[5] != subcommand_ || count != count_ ||
                   part != pending_.size()) {
            discard(frame[2] != sequence_ || frame[4] != command_ ||
                    frame[5] != subcommand_ || count != count_ ? "identity_mismatch" : "out_of_order");
            return false;
        }
        pending_.push_back(frame);
        if (pending_.size() == count_) {
#ifdef PANELAN_PRESET_TRACE
            emit("multipart_complete", "none", sequence_, command_, subcommand_, count_, pending_.size());
#endif
            complete.swap(pending_);
            return true;
        }
#ifdef PANELAN_PRESET_TRACE
        emit("multipart_progress", "none", sequence_, command_, subcommand_, count_, pending_.size());
#endif
        return false;
    }

private:
    void discard(const char *reason) {
#ifdef PANELAN_PRESET_TRACE
        if (!pending_.empty())
            emit("multipart_discard", reason, sequence_, command_, subcommand_, count_, pending_.size());
#else
        (void)reason;
#endif
        pending_.clear();
    }
    Frames pending_;
    uint8_t sequence_ = 0, command_ = 0, subcommand_ = 0, count_ = 0;
#ifdef PANELAN_PRESET_TRACE
    SparkReceiveTraceSink trace_ = nullptr;
    void reject(const Frame &frame, const char *reason) const {
        const bool header = frame.size() >= 6 && frame[0] == 0xF0 && frame[1] == 0x01;
        emit("incoming_reject", reason, header ? frame[2] : uint8_t(0),
             header ? frame[4] : uint8_t(0), header ? frame[5] : uint8_t(0), 0, 0);
    }
    void emit(const char *event, const char *reason, uint8_t message, uint8_t command,
              uint8_t subcommand, size_t expected, size_t received) const {
        if (trace_) trace_({event, reason, message, command, subcommand, expected, received});
    }
#endif
};

#endif
