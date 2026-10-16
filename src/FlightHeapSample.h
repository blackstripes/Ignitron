#pragma once
#include <stdint.h>

namespace FlightHeapSample {
// Two unsigned 8-bit quantities in units of 4096 bytes (round down).
// Clamp independently so unusually large heaps cannot wrap.
inline uint8_t units(uint32_t bytes) {
    const uint32_t blocks = bytes / 4096u;
    return blocks > 255u ? 255u : static_cast<uint8_t>(blocks);
}
inline uint16_t pack(uint32_t freeBytes, uint32_t minFreeBytes) {
    return uint16_t(units(freeBytes)) | (uint16_t(units(minFreeBytes)) << 8);
}
}
