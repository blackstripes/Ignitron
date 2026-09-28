#pragma once
#include <stddef.h>
#include <stdint.h>

// Called at the byte immediately after the seven-pedal body. Legacy presets
// end in one checksum byte. NEO Core adds two MessagePack float32 values first.
// Their meaning is not needed for slot names; do not discard them from the sum
// or mistake their 0xCA type marker for the checksum. Validate ORIGINAL bytes:
// re-encoding a Preset rounds floats and does not preserve all wire fields.
inline bool readSparkPresetChecksum(const uint8_t *payload, size_t size,
                                    size_t bodyEnd, uint8_t &checksum) {
    if (!payload || bodyEnd < 2 || bodyEnd >= size) return false;
    const size_t remaining = size - bodyEnd;
    if (remaining != 1 &&
        !(remaining == 11 && payload[bodyEnd] == 0xCA && payload[bodyEnd + 5] == 0xCA))
        return false;
    uint8_t sum = 0;
    // The two address bytes (current/hardware and slot) are not preset data.
    for (size_t i = 2; i < size - 1; ++i) sum = static_cast<uint8_t>(sum + payload[i]);
    if (sum != payload[size - 1]) return false;
    checksum = sum;
    return true;
}
