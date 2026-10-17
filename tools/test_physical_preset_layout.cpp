#include <cassert>
#include <cstdint>
#include <vector>
#include "controller/PhysicalSwitchRouting.h"

enum class View { Preset, Fx, Looper, Tuner, Device };
enum class Phase { Ready, Reconnecting };
enum class Capability { Unknown, Verified };
struct Snapshot {
    bool tunerActive = false, sparkStateStale = false;
    Phase connectionPhase = Phase::Ready;
    Capability looperCapability = Capability::Unknown;
    uint8_t confirmedHardwarePreset = 0;
};
struct UI {
    View view = View::Preset;
    View activeView() const { return view; }
    void physicalNavigate(View next) { view = next; }
    void physicalTunerPress(bool) {}
};
struct Actions {
    std::vector<int> presets, fx;
    int taps = 0;
    void requestHardwarePreset(int target) { presets.push_back(target); }
    void requestFxToggle(int slot) { fx.push_back(slot); }
    void requestTapTempo() { ++taps; }
};
int main() {
    using PhysicalPresetLayout::Role;
    const auto &p = PhysicalPresetLayout::positions;
    assert(p[0].role == Role::Slot && p[0].slot == 4);
    assert(p[1].role == Role::BankPrevious && p[2].role == Role::BankNext);
    assert(p[3].role == Role::Slot && p[3].slot == 1);
    assert(p[4].role == Role::Slot && p[4].slot == 2);
    assert(p[5].role == Role::Slot && p[5].slot == 3);
    PhysicalSwitchRouting routing;
    Snapshot s;
    UI ui;
    Actions a;
    auto press = [&](int key, int count = 8, PhysicalSwitches::Kind kind = PhysicalSwitches::Kind::Short) {
        routing.event(key, kind, count, s, ui, a);
    };
    press(1); assert(routing.browsedBank() == 0);
    for (int key : {0, 3, 4, 5}) press(key);
    assert((a.presets == std::vector<int>{4, 1, 2, 3}));
    press(2); assert(routing.browsedBank() == 1);
    for (int key : {0, 3, 4, 5}) press(key);
    assert((a.presets == std::vector<int>{4, 1, 2, 3, 8, 5, 6, 7}));
    press(2); assert(routing.browsedBank() == 1);
    press(1); assert(routing.browsedBank() == 0);
    press(2, 5); assert(routing.browsedBank() == 1);
    for (int key : {0, 4, 5}) press(key, 5);
    assert(a.presets.size() == 8); // partial bank disables P4/P2/P3
    press(3, 5); assert(a.presets.back() == 5 && a.presets.size() == 9);
    press(2, 5); assert(routing.browsedBank() == 1);
    press(1, 5); assert(routing.browsedBank() == 0);
    press(2, 4); assert(routing.browsedBank() == 0);
    press(6); assert(ui.view == View::Fx);
    for (int key = 0; key < 6; ++key) press(key);
    assert((a.fx == std::vector<int>{0, 1, 2, 3, 4, 5}));
    press(7); // TAP, not an FX or Preset key
    assert(a.fx.size() == 6 && a.presets.size() == 9);
    press(6); assert(ui.view == View::Preset); // MODE still cycles from FX
    press(7, 8, PhysicalSwitches::Kind::Long); assert(ui.view == View::Tuner); // TAP/TUNER long press
    press(0); press(7); // Performance/tap actions are suppressed in tuner view.
    assert(a.fx.size() == 6 && a.presets.size() == 9 && a.taps == 1);
}
