#include "../src/BleChunkWrite.h"

#include <cassert>
#include <cstdint>
#include <vector>

int main() {
    const std::vector<uint8_t> input{0, 1, 2, 3, 4, 5, 6, 7};
    const std::vector<uint8_t> original = input;
    std::vector<std::vector<uint8_t>> writes;
    const auto middleFailure = writeBleChunks(input, 3, [&](const uint8_t *data, std::size_t size) {
        writes.emplace_back(data, data + size);
        return writes.size() != 2;
    });
    assert(!middleFailure.success);
    assert(middleFailure.failedChunkIndex == 1 && middleFailure.chunkCount == 3 &&
           middleFailure.attemptedChunkCount == 2);
    assert((writes == std::vector<std::vector<uint8_t>>{{0, 1, 2}, {3, 4, 5}}));
    assert(input == original);

    writes.clear();
    const auto earlyFailure = writeBleChunks(input, 3, [&](const uint8_t *data, std::size_t size) {
        writes.emplace_back(data, data + size);
        return false;
    });
    assert(!earlyFailure.success && earlyFailure.failedChunkIndex == 0 &&
           earlyFailure.chunkCount == 3 && earlyFailure.attemptedChunkCount == 1);
    assert((writes == std::vector<std::vector<uint8_t>>{{0, 1, 2}}));

    writes.clear();
    const auto success = writeBleChunks(input, 3, [&](const uint8_t *data, std::size_t size) {
        writes.emplace_back(data, data + size);
        return true;
    });
    assert(success.success && success.chunkCount == 3 && success.attemptedChunkCount == 3);
    assert((writes == std::vector<std::vector<uint8_t>>{{0, 1, 2}, {3, 4, 5}, {6, 7}}));
    assert(input == original);

    const std::vector<uint8_t> empty;
    const auto emptyResult = writeBleChunks(empty, 3, [](const uint8_t *, std::size_t) {
        assert(false && "empty input should not write");
        return false;
    });
    assert(emptyResult.success && emptyResult.chunkCount == 0 && emptyResult.attemptedChunkCount == 0);
}
