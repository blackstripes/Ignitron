#include "controller/MiniActionPresentation.h"
#include "controller/PresetTargetQueue.h"
#include "controller/PhysicalPresetLayout.h"
#include <cassert>

int main() {
    const auto order = PhysicalPresetLayout::serviceOrder(0, 2, 4);
    assert(order[0] == 4 && order[1] == 1); // global slots 2, 4
    const auto bank = PhysicalPresetLayout::serviceOrder(1, 6, 4);
    assert(bank[0] == 4 && bank[1] == 0); // prior slot on another bank is off-screen
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
    assert(!s.sparkStateStale && fx.known); // main may show desired OFF, but confirmed is still ON
    fx.pending = false; // confirmation: Spark observation wins
    assert(MiniActionPresentation::fxEnabled(fx));
    fx.actionFailed = true; // timeout/conflict: no optimistic value survives
    assert(MiniActionPresentation::fxEnabled(fx));
    fx.enabled = false;
    s.sparkStateStale = true; // disconnect: no pending appearance is permitted
    fx.pending = true;
    fx.pendingDesiredEnabled = true;
    assert(!MiniActionPresentation::fxAuthoritative(s, 0));
    fx.pending = false;
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

    // Main card uses only a validated slot cache for optimistic/loading names;
    // Spark's old active name/description must not leak into either phase.
    using MiniActionPresentation::MainPresetPhase;
    using MiniActionPresentation::mainPresetCard;
    using MiniActionPresentation::presetSelected;
    MiniActionPresentation::MainConnectionPresentation link;
    ControllerSnapshot card;
    card.connectionPhase = ControllerConnectionPhase::Syncing;
    link.observe(card);
    assert(!MiniActionPresentation::mainShowsConnected(card, mainPresetCard(card).phase, link));
    card.identityKnown = true;
    card.ampName = "Spark 2";
    card.ampSerial = "A";
    card.pendingHardwarePreset = 2;
    link.observe(card); // accepted during startup's first full-preset sync
    assert(mainPresetCard(card).phase == MainPresetPhase::Pending);
    assert(!MiniActionPresentation::mainShowsConnected(card, mainPresetCard(card).phase, link));
    card.pendingHardwarePreset = 0;
    card.confirmedHardwarePreset = 2;
    card.locallyVerifiedHardwarePreset = 2;
    link.observe(card);
    assert(mainPresetCard(card).phase == MainPresetPhase::Loading);
    assert(!MiniActionPresentation::mainShowsConnected(card, mainPresetCard(card).phase, link));
    card.locallyVerifiedHardwarePreset = 0;
    assert(!MiniActionPresentation::mainShowsConnected(card, mainPresetCard(card).phase, link));
    card.connectionPhase = ControllerConnectionPhase::Reconnecting;
    card.pendingHardwarePreset = 2;
    link.observe(card);
    assert(!MiniActionPresentation::mainShowsConnected(card, mainPresetCard(card).phase, link));
    card.pendingHardwarePreset = 0;
    card.connectionPhase = ControllerConnectionPhase::Ready;
    link.observe(card);
    assert(link.readySeen);
    card.sparkStateStale = false;
    card.confirmedHardwarePreset = 2;
    card.presetName = "Old Spark tone";
    card.presetDescription = "Old description";
    for (uint8_t target = 1; target <= 8; ++target) {
        card.hardwarePresetNames[target - 1] = "Cached " + std::to_string(target);
        card.pendingHardwarePreset = target;
        auto view = mainPresetCard(card);
        assert(view.phase == MainPresetPhase::Pending && view.target == target);
        assert(presetSelected(card, target) && !presetSelected(card, target == 1 ? 2 : 1));
        assert(card.confirmedHardwarePreset == (target == 1 ? 2 : target - 1));
        assert(view.name == card.hardwarePresetNames[target - 1] && view.name != card.presetName);
        card.connectionPhase = ControllerConnectionPhase::Syncing;
        link.observe(card);
        assert(MiniActionPresentation::mainShowsConnected(card, view.phase, link)); // post-Ready Pending
        card.sparkStateStale = true;
        card.confirmedHardwarePreset = target;
        card.pendingHardwarePreset = 0;
        card.locallyVerifiedHardwarePreset = target;
        view = mainPresetCard(card);
        assert(view.phase == MainPresetPhase::Loading && view.name == "Cached " + std::to_string(target));
        assert(MiniActionPresentation::mainShowsConnected(card, view.phase, link));
        assert(presetSelected(card, target) && !presetSelected(card, 0));
        card.connectionPhase = ControllerConnectionPhase::Ready;
        link.observe(card);
        card.sparkStateStale = false;
        card.locallyVerifiedHardwarePreset = 0;
        card.presetName = "Spark " + std::to_string(target);
        view = mainPresetCard(card);
        assert(view.phase == MainPresetPhase::Ready && view.name == card.presetName);
        assert(MiniActionPresentation::mainShowsConnected(card, view.phase, link));
        assert(presetSelected(card, target));
    }
    card.pendingHardwarePreset = 7; // latest deferred selection wins
    card.confirmedHardwarePreset = 8;
    assert(mainPresetCard(card).name == "Cached 7");
    assert(presetSelected(card, 7) && !presetSelected(card, 8));
    card.pendingHardwarePreset = 3;
    assert(mainPresetCard(card).name == "Cached 3");
    assert(presetSelected(card, 3) && !presetSelected(card, 7));
    card.hardwarePresetNames[2].clear();
    assert(mainPresetCard(card).name == "PRESET 3");
    card.hardwarePresetNames.fill(""); // disconnect/identity change revokes validated cache
    assert(mainPresetCard(card).name == "PRESET 3");
    card.pendingHardwarePreset = 0;
    card.presetActionFailed = true;
    card.confirmedHardwarePreset = 2;
    card.presetName = "Last authoritative tone";
    assert(mainPresetCard(card).phase == MainPresetPhase::Ready);
    assert(mainPresetCard(card).name == card.presetName);
    card.failedHardwarePreset = 3;
    assert(presetSelected(card, 2) && !presetSelected(card, 3)); // timeout restores confirmed
    assert(MiniActionPresentation::presetFailed(card, 3));
    card.pendingHardwarePreset = 7; // newer accepted intent supersedes the failed slot
    assert(presetSelected(card, 7) && !presetSelected(card, 2));
    card.pendingHardwarePreset = 0;
    card.connectionPhase = ControllerConnectionPhase::Reconnecting;
    link.observe(card);
    assert(!link.readySeen);
    card.sparkStateStale = true;
    card.locallyVerifiedHardwarePreset = 0;
    assert(mainPresetCard(card).phase == MainPresetPhase::Stale);
    assert(!presetSelected(card, 2));
    card.pendingHardwarePreset = 3; // even an inconsistent stale snapshot cannot claim a selection
    assert(mainPresetCard(card).phase == MainPresetPhase::Stale);
    assert(!presetSelected(card, 3));
    card.pendingHardwarePreset = 0;
    card.connectionPhase = ControllerConnectionPhase::Syncing;
    link.observe(card);
    card.pendingHardwarePreset = 3;
    assert(!MiniActionPresentation::mainShowsConnected(card, mainPresetCard(card).phase, link));
    card.pendingHardwarePreset = 0;
    assert(mainPresetCard(card).phase == MainPresetPhase::Stale); // no link-scoped number proof
    card.locallyVerifiedHardwarePreset = 2;
    assert(mainPresetCard(card).phase == MainPresetPhase::Loading);
    assert(mainPresetCard(card).name == "PRESET 2");
    assert(presetSelected(card, 2) && !presetSelected(card, 3));
    assert(MiniActionPresentation::presetOtherGrey(card));
    card.connectionPhase = ControllerConnectionPhase::Reconnecting;
    assert(!MiniActionPresentation::presetOtherGrey(card));

    card.connectionPhase = ControllerConnectionPhase::Ready;
    link.observe(card);
    card.connectionPhase = ControllerConnectionPhase::Syncing;
    card.pendingHardwarePreset = 2;
    card.ampSerial = "B"; // different amp on a still-present link
    link.observe(card);
    assert(!link.readySeen);
    assert(!MiniActionPresentation::mainShowsConnected(card, mainPresetCard(card).phase, link));

    assert(!MiniActionPresentation::needsRedraw(true, true, "same", "same"));
    assert(MiniActionPresentation::needsRedraw(true, true, "old", "new"));
    assert(MiniActionPresentation::needsRedraw(false, true, "same", "same"));
    assert(MiniActionPresentation::needsRedraw(true, false, "same", "same"));
}
