#pragma once
#include "ControllerState.h"
#include <cstring>

// Derived from the controller's authoritative snapshot; never a second state store.
namespace MiniActionPresentation {
constexpr uint32_t kFailureBadgeMs = 1500;
inline bool failureBadgeExpired(uint32_t now, uint32_t failedAt) {
    return static_cast<uint32_t>(now - failedAt) >= kFailureBadgeMs;
}
inline uint8_t failedTargetAfterFailure(const ControllerSnapshot &s) {
    return s.pendingHardwarePreset ? s.pendingHardwarePreset : s.failedHardwarePreset;
}
inline bool presetConfirmed(const ControllerSnapshot &s, uint8_t target) {
    return target != 0 && s.confirmedHardwarePreset == target &&
           ((!s.sparkStateStale && s.connectionPhase == ControllerConnectionPhase::Ready) ||
            (s.connectionPhase == ControllerConnectionPhase::Syncing &&
             s.locallyVerifiedHardwarePreset == target));
}
inline bool fxConfirmed(const ControllerSnapshot &s, uint8_t slot) {
    const auto &fx = s.fxSlots[slot];
    return !s.sparkStateStale && fx.known && !fx.pending && !fx.actionFailed;
}
inline bool fxConfirmedDirection(const ControllerSnapshot &s, uint8_t slot, bool desiredEnabled) {
    return fxConfirmed(s, slot) && s.fxSlots[slot].enabled == desiredEnabled;
}
inline bool fxAuthoritative(const ControllerSnapshot &s, uint8_t slot) {
    return !s.sparkStateStale && s.fxSlots[slot].known && !s.fxSlots[slot].pending;
}
inline bool fxEnabled(const ControllerFxSlot &slot) {
    return slot.pending ? slot.pendingDesiredEnabled : slot.enabled;
}
inline bool presetFailed(const ControllerSnapshot &s, uint8_t target) {
    return s.presetActionFailed && s.failedHardwarePreset == target && s.pendingHardwarePreset != target;
}
inline bool needsRedraw(bool drawn, bool sameView, const char *previous, const char *current) {
    return !drawn || !sameView || std::strcmp(previous, current) != 0;
}
} // namespace MiniActionPresentation
