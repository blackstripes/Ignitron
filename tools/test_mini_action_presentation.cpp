#include "controller/MiniActionPresentation.h"
#include "controller/PresetTargetQueue.h"
#include <cassert>

int main() {
    ControllerSnapshot s;
    s.connectionPhase = ControllerConnectionPhase::Ready;
    s.sparkStateStale = false;
    s.confirmedHardwarePreset = 2;
    auto &fx = s.fxSlots[0];
    fx.known = true;
    fx.enabled = false;
    fx.pending = true;
    fx.pendingDesiredEnabled = true;
    assert(MiniActionPresentation::fxEnabled(fx) && !fx.enabled);
    fx.enabled = true;
    fx.pendingDesiredEnabled = false;
    assert(!MiniActionPresentation::fxEnabled(fx) && fx.enabled);
    fx.pending = false; // confirmation: Spark observation wins
    assert(MiniActionPresentation::fxEnabled(fx));
    fx.actionFailed = true; // timeout/conflict: no optimistic value survives
    assert(MiniActionPresentation::fxEnabled(fx));
    fx.enabled = false;
    s.sparkStateStale = true; // disconnect: no pending appearance is permitted
    assert(!fx.pending && !MiniActionPresentation::fxEnabled(fx));

    s.pendingHardwarePreset = 4;
    assert(s.confirmedHardwarePreset == 2 && !MiniActionPresentation::presetFailed(s, 4));
    // refreshFromSpark disconnect clears pending and captures its target;
    // onAmpDisconnected may then call failHardwarePresetRequest again.
    s.failedHardwarePreset = MiniActionPresentation::failedTargetAfterFailure(s);
    s.pendingHardwarePreset = 0;
    s.presetActionFailed = true;
    s.failedHardwarePreset = MiniActionPresentation::failedTargetAfterFailure(s);
    assert(s.failedHardwarePreset == 4 && MiniActionPresentation::presetFailed(s, 4));
    s.pendingHardwarePreset = 3;
    assert(MiniActionPresentation::failedTargetAfterFailure(s) == 3);
    s.failedHardwarePreset = 4;
    s.presetActionFailed = true;
    s.pendingHardwarePreset = 0;
    assert(MiniActionPresentation::presetFailed(s, 4));
    assert(!MiniActionPresentation::presetFailed(s, 2));
    s.pendingHardwarePreset = 3; // newer deferred intent outranks old failure
    assert(!MiniActionPresentation::presetFailed(s, 3));

    // Matching hardware-number proof selects the preset while its full chain
    // is invalidated; it must not make unknown FX slots look confirmed.
    s.connectionPhase = ControllerConnectionPhase::Syncing;
    s.sparkStateStale = true;
    s.confirmedHardwarePreset = 3;
    s.locallyVerifiedHardwarePreset = 3;
    s.pendingHardwarePreset = 0;
    assert(MiniActionPresentation::presetConfirmed(s, 3));
    assert(!MiniActionPresentation::fxConfirmed(s, 0));
    s.connectionPhase = ControllerConnectionPhase::Reconnecting;
    s.locallyVerifiedHardwarePreset = 0; // dropped link, cached number still 3
    assert(!MiniActionPresentation::presetConfirmed(s, 3));
    s.connectionPhase = ControllerConnectionPhase::Syncing; // new link, no number proof
    assert(!MiniActionPresentation::presetConfirmed(s, 3));
    s.connectionPhase = ControllerConnectionPhase::Ready;
    s.sparkStateStale = false;
    assert(MiniActionPresentation::presetConfirmed(s, 3));
    fx.known = true;
    fx.pending = false;
    fx.actionFailed = false;
    assert(MiniActionPresentation::fxConfirmed(s, 0));
    // The action confirmed ON while off-screen. A later opposite Spark OFF
    // observation is authoritative but cannot complete that action's mini_final.
    const bool requestedOn = true;
#ifdef PANELAN_PRESET_TRACE
    s.miniActionId = 27;
    s.miniActionFx = true;
    s.miniActionConfirmed = true;
    s.miniActionDesiredEnabled = requestedOn;
#endif
    fx.enabled = false;
    assert(MiniActionPresentation::fxConfirmed(s, 0));
    assert(!MiniActionPresentation::fxConfirmedDirection(s, 0, requestedOn));
#ifdef PANELAN_PRESET_TRACE
    assert(!MiniActionPresentation::fxConfirmedDirection(s, 0, s.miniActionDesiredEnabled));
    assert(s.miniActionId == 27); // an opposite draw must not consume the ID
#endif
    fx.enabled = true;
    assert(MiniActionPresentation::fxConfirmedDirection(s, 0, requestedOn));
    s.sparkStateStale = true;
    assert(!MiniActionPresentation::fxConfirmedDirection(s, 0, requestedOn));
    s.sparkStateStale = false;
    fx.known = false;
    assert(!MiniActionPresentation::fxConfirmedDirection(s, 0, requestedOn));
    assert(!MiniActionPresentation::fxConfirmed(s, 0));
    // Conflict/failure: Spark's observed ON/OFF still drives the card, with a
    // separate error badge. Unknown/stale must never assert either direction.
    fx.known = true;
    fx.enabled = true;
    fx.actionFailed = true;
    assert(MiniActionPresentation::fxAuthoritative(s, 0));
    assert(MiniActionPresentation::fxEnabled(fx));
    assert(!MiniActionPresentation::fxConfirmed(s, 0));
    s.sparkStateStale = true;
    assert(!MiniActionPresentation::fxAuthoritative(s, 0));
    s.sparkStateStale = false;
    fx.known = false;
    assert(!MiniActionPresentation::fxAuthoritative(s, 0));
    constexpr uint32_t failedAt = 0xfffffff0u;
    assert(!MiniActionPresentation::failureBadgeExpired(failedAt + 1499u, failedAt));
    assert(MiniActionPresentation::failureBadgeExpired(failedAt + 1500u, failedAt));
    s.presetActionFailed = false;
    s.failedHardwarePreset = 0; // controller expiry clears both together
    fx.actionFailed = false;
    assert(!MiniActionPresentation::presetFailed(s, 3) && !fx.actionFailed);
    s.presetActionFailed = false;
    s.confirmedHardwarePreset = 3;
    s.pendingHardwarePreset = 0;
    assert(!MiniActionPresentation::presetFailed(s, 3));

    assert(!MiniActionPresentation::needsRedraw(true, true, "same", "same"));
    assert(MiniActionPresentation::needsRedraw(true, true, "old", "new"));
    assert(MiniActionPresentation::needsRedraw(false, true, "same", "same"));
    assert(MiniActionPresentation::needsRedraw(true, false, "same", "same"));
}
