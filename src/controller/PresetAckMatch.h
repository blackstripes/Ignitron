#pragma once
#include <cstdint>

// ACKs are transport milestones only. Zero means no outstanding switch.
inline bool matchesHardwarePresetAck(uint8_t outstanding, uint8_t subcmd, uint8_t msgNum) {
    return outstanding != 0 && subcmd == 0x38 && msgNum == outstanding;
}
