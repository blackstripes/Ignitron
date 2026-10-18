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
// Main-card phases only: cached slot names are display metadata, never proof
// of an active preset or of a refreshed signal chain.
enum class MainPresetPhase : uint8_t { Stale, Pending, Loading, Ready };
struct MainPresetCard {
    MainPresetPhase phase;
    uint8_t target;
    std::string name;
};
inline MainPresetCard mainPresetCard(const ControllerSnapshot &s) {
    const bool connected = s.connectionPhase == ControllerConnectionPhase::Identifying ||
                           s.connectionPhase == ControllerConnectionPhase::Syncing ||
                           s.connectionPhase == ControllerConnectionPhase::Ready;
    MainPresetPhase phase = MainPresetPhase::Stale;
    uint8_t target = s.confirmedHardwarePreset;
    if (connected && s.pendingHardwarePreset >= 1 && s.pendingHardwarePreset <= s.hardwarePresetNames.size()) {
        phase = MainPresetPhase::Pending;
        target = s.pendingHardwarePreset;
    } else if (connected && s.connectionPhase == ControllerConnectionPhase::Syncing &&
               target >= 1 && target <= s.hardwarePresetNames.size() &&
               s.locallyVerifiedHardwarePreset == target) {
        phase = MainPresetPhase::Loading;
    } else if (s.connectionPhase == ControllerConnectionPhase::Ready && !s.sparkStateStale) {
        phase = MainPresetPhase::Ready;
    }
    std::string name;
    if (phase == MainPresetPhase::Pending || phase == MainPresetPhase::Loading) {
        name = s.hardwarePresetNames[target - 1];
        if (name.empty()) name = "PRESET " + std::to_string(target);
    } else if (!s.presetName.empty()) {
        name = s.presetName;
    } else if (target != 0) {
        name = "PRESET " + std::to_string(target);
    } else {
        name = connected ? "SYNCING PRESET" : "YOUR TONE";
    }
    return {phase, target, name};
}
// A selected body is an intent during Pending, number proof during Loading,
// and Spark authority during Ready. Only the Pending phase carries an amber cue.
inline bool presetSelected(const ControllerSnapshot &s, uint8_t target) {
    if (!target) return false;
    const auto card = mainPresetCard(s);
    return card.phase != MainPresetPhase::Stale && card.target == target;
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
