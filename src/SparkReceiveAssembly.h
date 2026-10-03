#ifndef SPARK_RECEIVE_ASSEMBLY_H
#define SPARK_RECEIVE_ASSEMBLY_H

#include <cstdint>
#include <vector>

// Join BLE fragments into complete wire frames. Do not interpret a fragment
// boundary as a frame boundary: F0 01 may also be the sequence/checksum pair
// immediately following a split start marker. SparkMessage::buildChunkData
// XORs the seven-bit data bytes into the frame checksum.
class SparkReceiveFrames {
public:
    using Frame = std::vector<uint8_t>;
    using Frames = std::vector<Frame>;

    void reset() { partial_.clear(); }
    Frames accept(const Frame &fragment) {
        Frames ready;
        for (uint8_t value : fragment) {
            if (partial_.empty() && value != 0xF0) continue;
            if (partial_.size() == 1 && value != 0x01) {
                reset();
                if (value != 0xF0) continue;
            }
            partial_.push_back(value);
            if (value == 0xF7) {
                // A stale incomplete frame may precede a fresh frame in this
                // buffer. Prefer the earliest valid candidate; only seek a
                // later F0 01 if the earlier one fails its wire checksum.
                for (size_t pos = 0; pos + 6 < partial_.size(); ++pos) {
                    if (partial_[pos] != 0xF0 || partial_[pos + 1] != 0x01 ||
                        (partial_[pos + 4] & 0x80) || (partial_[pos + 5] & 0x80)) continue;
                    uint8_t sum = 0;
                    bool sevenBit = true;
                    for (size_t i = pos + 6; i + 1 < partial_.size(); ++i) {
                        if (partial_[i] & 0x80) sevenBit = false;
                        sum ^= partial_[i];
                    }
                    if (!sevenBit || sum != partial_[pos + 3]) continue;
                    ready.emplace_back(partial_.begin() + pos, partial_.end());
                    break;
                }
                reset();
            } else if (partial_.size() > 1024) {
                // Bounded recovery from a lost F7; keep the most recent start
                // rather than growing indefinitely on a damaged BLE stream.
                size_t start = partial_.size();
                for (size_t i = 1; i + 1 < partial_.size(); ++i)
                    if (partial_[i] == 0xF0 && partial_[i + 1] == 0x01) start = i;
                if (start < partial_.size())
                    partial_.erase(partial_.begin(), partial_.begin() + start);
                else reset();
            }
        }
        return ready;
    }

private:
    Frame partial_;
};

// Accepts complete F0 01 ... F7 frames. The encoder adds a [count, part,
// chunkLength] prefix only when count > 1. Unrelated single-frame notifications
// do not disturb a pending preset.
class SparkReceiveAssembly {
public:
    using Frame = std::vector<uint8_t>;
    using Frames = std::vector<Frame>;

    void reset() { pending_.clear(); }

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
            reset();
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
            reset();
            return false;
        }
        if (shape == 1) {
            // An unprefixed single-frame preset is a new logical message too;
            // never allow an older multipart response to resume after it.
            reset();
            complete.push_back(frame);
            return true;
        }
        const uint8_t count = data[0], part = data[1];
        // A new start supersedes a stale incomplete response. A non-start
        // with a different identity cannot borrow bytes from the old one.
        if (part == 0 && !pending_.empty()) {
            const bool duplicate = frame[2] == sequence_ && frame[4] == command_ &&
                                   frame[5] == subcommand_ && count == count_;
            reset();
            if (duplicate) return false;
        }
        if (pending_.empty()) {
            if (part != 0) return false;
            sequence_ = frame[2];
            command_ = frame[4];
            subcommand_ = frame[5];
            count_ = count;
        } else if (frame[2] != sequence_ || frame[4] != command_ ||
                   frame[5] != subcommand_ || count != count_ ||
                   part != pending_.size()) {
            reset();
            return false;
        }
        pending_.push_back(frame);
        if (pending_.size() == count_) {
            complete.swap(pending_);
            return true;
        }
        return false;
    }

private:
    Frames pending_;
    uint8_t sequence_ = 0, command_ = 0, subcommand_ = 0, count_ = 0;
};

#endif
