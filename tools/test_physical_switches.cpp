#include <cassert>
#include <vector>
#include "PanelLanMiniMCP23017.h"
#include "controller/PhysicalSwitchRouting.h"

TestWire Wire;
TestSerial Serial;
enum class View { Preset, Fx, Looper, Tuner, Device };
enum class Phase { Ready, Reconnecting };
enum class Capability { Unknown, Verified };
struct Snapshot { bool tunerActive = false, sparkStateStale = false; Phase connectionPhase = Phase::Ready; Capability looperCapability = Capability::Unknown; uint8_t confirmedHardwarePreset = 0; };
struct UI {
    View view = View::Preset;
    int exits = 0;
    int cancellations = 0;
    View beforeTuner = View::Preset;
    View activeView() const { return view; }
    void physicalNavigate(View to) {
        if (to == View::Tuner && view != View::Tuner) beforeTuner = view;
        view = to;
    }
    void physicalTunerPress(bool active) {
        if (active) ++exits;
        else { ++cancellations; view = beforeTuner; }
    }
};
struct Actions {
    std::vector<int> presets, fx;
    int taps = 0;
    bool requestHardwarePreset(int n) { presets.push_back(n); return true; }
    bool requestFxToggle(int n) { fx.push_back(n); return true; }
    bool requestTapTempo() { ++taps; return true; }
};
int main() {
    PanelLanMiniMCP23017 mcp;
    assert(mcp.begin());
    assert(Wire.writes[0].reg == 0x15 && Wire.writes[0].value == 0x7f);
    assert(Wire.writes[1].reg == 0x01 && Wire.writes[1].value == 0x80);
    assert(Wire.writes[2].reg == 0x00 && Wire.writes[2].value == 0xff);
    assert(Wire.writes[3].reg == 0x0c && Wire.writes[3].value == 0xff);
    uint8_t pressed = 0;
    for (int i = 0; i < 8; ++i) {
        Wire.gpio = static_cast<uint8_t>(~(1u << i));
        assert(mcp.readSwitches(pressed) && pressed == (1u << i) && Wire.reg == 0x12);
    }
    Wire.fail = true;
    pressed = 0x55;
    assert(!mcp.readSwitches(pressed) && pressed == 0x55);
    Wire.fail = false;
    assert(mcp.readSwitches(pressed));
    PanelLanMiniMCP23017 recovered;
    Wire.fail = true;
    assert(!recovered.begin() && !recovered.readSwitches(pressed));
    Wire.fail = false;
    assert(recovered.begin() && recovered.readSwitches(pressed));

    PhysicalSwitches keys;
    std::vector<int> events;
    auto emit = [&](uint8_t i, PhysicalSwitches::Kind k) { events.push_back(i * 2 + (k == PhysicalSwitches::Kind::Long)); };
    keys.sample(true, 1, 0, emit); // boot held, release must not produce short
    keys.sample(true, 0, 50, emit);
    keys.sample(true, 1, 60, emit); keys.sample(true, 0, 65, emit); // bounce
    keys.sample(true, 3, 100, emit); keys.sample(true, 3, 131, emit);
    keys.sample(true, 1, 200, emit); keys.sample(true, 1, 231, emit);
    keys.sample(true, 0, 250, emit); keys.sample(true, 0, 281, emit);
    assert((events == std::vector<int>{2, 0}));
    events.clear();
    keys.sample(true, 4, 400, emit); keys.sample(true, 4, 431, emit);
    keys.sample(true, 4, 1431, emit); keys.sample(true, 0, 1440, emit); keys.sample(true, 0, 1471, emit);
    assert((events == std::vector<int>{5})); // long suppresses short
    events.clear();
    keys.sample(true, 1, 1500, emit); keys.sample(true, 1, 1531, emit);
    keys.sample(false, 0, 1550, emit); keys.sample(true, 1, 1600, emit);
    keys.sample(true, 0, 1650, emit); keys.sample(true, 1, 1700, emit);
    assert(events.empty()); // failure and boot-held both require all released
    keys.sample(true, 0, 1750, emit); keys.sample(true, 1, 1800, emit);
    keys.sample(true, 1, 1831, emit); keys.sample(true, 0, 1850, emit); keys.sample(true, 0, 1881, emit);
    assert((events == std::vector<int>{0}));

    PhysicalSwitchRouting route;
    Snapshot s;
    UI ui;
    Actions a;
    auto e = [&](int i, PhysicalSwitches::Kind k = PhysicalSwitches::Kind::Short, int count = 8) {
        route.event(i, k, count, s, ui, a);
    };
    using PhysicalPresetLayout::Role;
    const auto &positions = PhysicalPresetLayout::positions;
    assert(positions[0].role == Role::Slot && positions[0].slot == 4);
    assert(positions[1].role == Role::BankPrevious);
    assert(positions[2].role == Role::BankNext);
    assert(positions[3].role == Role::Slot && positions[3].slot == 1);
    assert(positions[4].role == Role::Slot && positions[4].slot == 2);
    assert(positions[5].role == Role::Slot && positions[5].slot == 3);
    s.confirmedHardwarePreset = 6;
    route.observe(s, 8);
    assert(route.browsedBank() == 1);
    e(1); assert(route.browsedBank() == 0);
    route.observe(s, 8); assert(route.browsedBank() == 0); // unchanged confirmation preserves browsing
    s.confirmedHardwarePreset = 7;
    route.observe(s, 8); assert(route.browsedBank() == 1);
    s.sparkStateStale = true;
    route.observe(s, 8);
    s.sparkStateStale = false;
    e(1); assert(route.browsedBank() == 0);
    route.observe(s, 8); assert(route.browsedBank() == 1); // fresh after reconnect
    s.confirmedHardwarePreset = 1;
    route.observe(s, 8); assert(route.browsedBank() == 0);
    e(1); assert(route.browsedBank() == 0); // previous disabled at first bank
    e(0); e(3); e(4); e(5);
    assert((a.presets == std::vector<int>{4, 1, 2, 3}));
    e(2); assert(route.browsedBank() == 1);
    e(0); e(3); e(4); e(5);
    assert((a.presets == std::vector<int>{4, 1, 2, 3, 8, 5, 6, 7}));
    e(2); assert(route.browsedBank() == 1); // next disabled at last bank
    e(1); assert(route.browsedBank() == 0);
    e(2, PhysicalSwitches::Kind::Short, 4); assert(route.browsedBank() == 0);
    e(2, PhysicalSwitches::Kind::Short, 5); assert(route.browsedBank() == 1);
    e(0, PhysicalSwitches::Kind::Short, 5); // unavailable P4 in partial bank
    e(3, PhysicalSwitches::Kind::Short, 5);
    assert(a.presets.back() == 5 && a.presets.size() == 9);
    e(1); assert(route.browsedBank() == 0);
    e(6); assert(ui.view == View::Fx);
    for (int i = 0; i < 6; ++i) e(i);
    assert((a.fx == std::vector<int>{0,1,2,3,4,5}));
    e(6); assert(ui.view == View::Preset);
    e(6); s.looperCapability = Capability::Verified; e(6); assert(ui.view == View::Looper);
    e(0); assert(a.presets.size() == 9 && a.fx.size() == 6); // looper inert
    e(6); assert(ui.view == View::Preset);
    e(7); assert(a.taps == 1);
    e(6); assert(ui.view == View::Fx);
    e(7, PhysicalSwitches::Kind::Long); assert(ui.view == View::Tuner && ui.beforeTuner == View::Fx);
    e(0); assert(ui.view == View::Fx && ui.cancellations == 1 && a.fx.size() == 6);
    e(7, PhysicalSwitches::Kind::Long); assert(ui.view == View::Tuner);
    e(6); assert(ui.view == View::Fx && ui.cancellations == 2);
    e(7, PhysicalSwitches::Kind::Long); assert(ui.view == View::Tuner);
    e(7); assert(ui.view == View::Fx && ui.cancellations == 3 && a.taps == 1);
    e(7, PhysicalSwitches::Kind::Long); assert(ui.view == View::Tuner);
    s.tunerActive = true;
    e(0); e(6); e(7); assert(ui.exits == 3 && a.taps == 1 && a.presets.size() == 9 && ui.view == View::Tuner);
    s.tunerActive = false; ui.view = View::Device;
    e(0); e(7); assert(a.taps == 2 && a.presets.size() == 9);
    ui.view = View::Preset; s.connectionPhase = Phase::Reconnecting; s.sparkStateStale = true;
    e(0); e(7); e(6); e(0);
    assert(a.presets.size() == 9 && a.fx.size() == 6 && a.taps == 2);
}
