#pragma once
#include "PhysicalPresetBank.h"
#include "PhysicalPresetLayout.h"
#include "PhysicalSwitches.h"

// UI/action adapter intentionally has no Spark transport dependency. View enum
// is ordered Preset, Fx, Looper, Tuner, Device by the PanelLan UI.
class PhysicalSwitchRouting {
public:
    uint8_t browsedBank() const { return bank_.bank(); }
    void clamp(uint8_t count) { bank_.clamp(count); }
    template<class Snapshot> void observe(const Snapshot &s, uint8_t count) {
        bank_.observe(s.confirmedHardwarePreset,
                      s.connectionPhase == decltype(s.connectionPhase)::Ready && !s.sparkStateStale, count);
    }
    template<class UI, class Actions, class Snapshot>
    void event(uint8_t key, PhysicalSwitches::Kind kind, uint8_t count,
               const Snapshot &s, UI &ui, Actions &actions) {
        bank_.clamp(count);
        const auto view = ui.activeView();
        if (s.tunerActive || view == decltype(view)::Tuner) {
            // An entry still pending has the same precedence as an active tuner.
            if (key < 8) ui.physicalTunerPress(s.tunerActive);
            return;
        }
        if (key == 6) {
            if (kind != PhysicalSwitches::Kind::Short) return;
            if (view == decltype(view)::Preset) ui.physicalNavigate(decltype(view)::Fx);
            else if (view == decltype(view)::Fx)
                ui.physicalNavigate(s.looperCapability == decltype(s.looperCapability)::Verified
                                        ? decltype(view)::Looper : decltype(view)::Preset);
            else if (view == decltype(view)::Looper) ui.physicalNavigate(decltype(view)::Preset);
            return;
        }
        if (key == 7) {
            if (kind == PhysicalSwitches::Kind::Long) {
                if (s.connectionPhase == decltype(s.connectionPhase)::Ready && !s.sparkStateStale)
                    ui.physicalNavigate(decltype(view)::Tuner);
            } else if (s.connectionPhase == decltype(s.connectionPhase)::Ready && !s.sparkStateStale)
                actions.requestTapTempo();
            return;
        }
        if (key >= 6 || kind != PhysicalSwitches::Kind::Short ||
            view == decltype(view)::Device || view == decltype(view)::Looper) return;
        if (view == decltype(view)::Fx) {
            if (s.connectionPhase == decltype(s.connectionPhase)::Ready && !s.sparkStateStale)
                actions.requestFxToggle(key);
            return;
        }
        if (view == decltype(view)::Preset) {
            const auto position = PhysicalPresetLayout::positions[key];
            if (position.role == PhysicalPresetLayout::Role::BankPrevious) bank_.step(-1, count);
            else if (position.role == PhysicalPresetLayout::Role::BankNext) bank_.step(1, count);
            else {
                const uint8_t target = bank_.target(position.slot - 1, count);
                if (target && s.connectionPhase == decltype(s.connectionPhase)::Ready && !s.sparkStateStale)
                    actions.requestHardwarePreset(target);
            }
        }
    }
private:
    PhysicalPresetBank bank_;
};
