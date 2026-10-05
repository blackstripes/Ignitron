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
        if (!partial_.empty()) trace("frame_discard", "reset");
#endif
        partial_.clear();
    }
    Frames accept(const Frame &fragment) {
        Frames ready;
        for (uint8_t value : fragment) {
            if (partial_.empty() && value != 0xF0) continue;
            if (partial_.size() == 1 && value != 0x01) {
#ifdef PANELAN_PRESET_TRACE
                trace("frame_discard", "malformed_start");
#endif
                partial_.clear();
                if (value != 0xF0) continue;
            }
            partial_.push_back(value);
            if (value == 0xF7) {
                // A stale incomplete frame may precede a fresh frame in this
                // buffer. Prefer the earliest valid candidate; only seek a
                // later F0 01 if the earlier one fails its wire checksum.
#ifdef PANELAN_PRESET_TRACE
                bool rejectedChecksum = false;
                bool rejectedInvalidWire = false;
                bool found = false;
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
#endif
                    break;
                }
#ifdef PANELAN_PRESET_TRACE
                if (rejectedChecksum) trace("frame_discard", found ? "checksum_resync" : "checksum");
                if (rejectedInvalidWire || (!found && !rejectedChecksum))
                    trace("frame_discard", found ? "invalid_wire_resync" : "invalid_wire");
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
                    trace("frame_discard", "overflow_resync");
#endif
                    partial_.erase(partial_.begin(), partial_.begin() + start);
                } else {
#ifdef PANELAN_PRESET_TRACE
                    trace("frame_discard", "overflow");
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
    void trace(const char *event, const char *reason) const {
        if (!trace_) return;
        const bool header = partial_.size() >= 6 && partial_[0] == 0xF0 && partial_[1] == 0x01;
        trace_({event, reason, header ? partial_[2] : uint8_t(0),
                header ? partial_[4] : uint8_t(0), header ? partial_[5] : uint8_t(0),
                0, partial_.size()});
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
