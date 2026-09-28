#pragma once

#include "PresetTargetQueue.h"
#include "PresetTimeoutReconcile.h"

// Drop both unsent intent and timeout verification at the BLE link boundary.
// Neither a deferred target nor a reply to the previous link's query may run
// when the next link becomes ready.
inline void resetPresetLinkPending(PresetTargetQueue &targets, PresetTimeoutReconcile &reconcile) {
    targets.clear();
    reconcile.reset();
    reconcile.cursor() = 0;
}
