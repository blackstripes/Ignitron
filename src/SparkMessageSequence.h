#pragma once

#include <cstdint>

// 0xEE is reserved for the serialized background hardware-slot read. Keep
// nextMessageNum usable *before* any normal command is constructed.
inline uint8_t nextNormalSparkMessageNumber(uint8_t used) {
    if (used == 0) return 2; // Spark encodes zero as wire sequence one.
    uint8_t next = static_cast<uint8_t>(used + 1);
    return next == 0xEE ? 0xEF : next;
}
