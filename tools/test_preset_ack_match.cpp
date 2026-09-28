#include "controller/PresetAckMatch.h"
#include <cassert>

int main() {
    assert(matchesHardwarePresetAck(7, 0x38, 7));
    assert(!matchesHardwarePresetAck(7, 0x38, 6)); // superseded switch
    assert(!matchesHardwarePresetAck(0, 0x38, 7)); // resolved / disconnected
    assert(!matchesHardwarePresetAck(7, 0x15, 7)); // unrelated ACK
}
