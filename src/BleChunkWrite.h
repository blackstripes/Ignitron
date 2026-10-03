#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

struct BleChunkWriteResult {
    bool success;
    std::size_t failedChunkIndex; // zero-based; equals chunkCount on success
    std::size_t chunkCount;
    std::size_t attemptedChunkCount; // includes the rejected chunk; zero for empty input
};

// maxChunkSize must be positive. Writer receives a read-only slice of the
// original bytes. Stop on the first rejected chunk; empty input succeeds.
template <typename Writer>
BleChunkWriteResult writeBleChunks(const std::vector<uint8_t> &cmd,
                                  std::size_t maxChunkSize, Writer writer) {
    const std::size_t count = cmd.empty() ? 0 : 1 + (cmd.size() - 1) / maxChunkSize;
    for (std::size_t offset = 0, index = 0; offset < cmd.size(); ++index) {
        const std::size_t size = std::min(maxChunkSize, cmd.size() - offset);
        if (!writer(cmd.data() + offset, size)) {
            return {false, index, count, index + 1};
        }
        offset += size;
    }
    return {true, count, count, count};
}
