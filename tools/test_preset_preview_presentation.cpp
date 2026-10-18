#include "controller/PresetPreviewPresentation.h"
#include <cassert>
#include <vector>

struct Pedal { std::string name; bool isOn; bool isOnKnown; };
struct Cached { bool isEmpty = false; std::string name = "Target", description = "Tone";
                uint8_t checksum = 42; std::vector<Pedal> pedals; };

int main() {
    using namespace PresetPreviewPresentation;
    ControllerSnapshot s;
    s.connectionPhase = ControllerConnectionPhase::Ready;
    s.identityKnown = true;
    s.ampName = "Spark 2"; s.ampSerial = "abc";
    s.confirmedHardwarePreset = 1; s.presetName = "Old";
    s.pendingHardwarePreset = 6;
    Evidence e;
    e.checksumsReady = e.slotPresent = true;
    e.cacheAmpName = s.ampName; e.cacheAmpSerial = s.ampSerial;
    e.checksum = 42; e.generationBefore = e.generationAfter = 3;
    Cached p;
    p.pedals.resize(7, {"effect", false, true}); p.pedals[5].isOn = true;
    auto preview = select(s, e, p);
    assert(preview.valid && preview.target == 6 && preview.name == "Target" && preview.enabled[4]);
    assert(s.presetName == "Old" && !s.fxSlots[4].enabled); // display is not command authority
    s.pendingHardwarePreset = 7;
    assert(select(s, e, p).target == 7); // old target cannot stick
    s.pendingHardwarePreset = 0; s.confirmedHardwarePreset = 7;
    s.connectionPhase = ControllerConnectionPhase::Syncing;
    s.locallyVerifiedHardwarePreset = 7; s.sparkStateStale = true;
    assert(select(s, e, p).valid);
    s.locallyVerifiedHardwarePreset = 0;
    assert(!select(s, e, p).valid);
    s.locallyVerifiedHardwarePreset = 7;
    e.slotPresent = false; assert(!select(s, e, p).valid); e.slotPresent = true;
    p.pedals[6].name.clear(); assert(!select(s, e, p).valid); p.pedals[6].name = "effect";
    p.pedals[5].isOnKnown = false; assert(!select(s, e, p).valid); p.pedals[5].isOnKnown = true;
    p.pedals[3].isOnKnown = false; assert(select(s, e, p).valid); // unmapped pedal
    p.isEmpty = true; assert(!select(s, e, p).valid); p.isEmpty = false;
    p.checksum++; assert(!select(s, e, p).valid); p.checksum--;
    e.generationAfter++; assert(!select(s, e, p).valid); e.generationAfter--;
    s.ampSerial = "other"; assert(!select(s, e, p).valid); s.ampSerial = "abc";
    s.ampName = "other"; assert(!select(s, e, p).valid); s.ampName = "Spark 2";
    s.connectionPhase = ControllerConnectionPhase::Reconnecting;
    assert(!select(s, e, p).valid);
    s.connectionPhase = ControllerConnectionPhase::Ready; s.sparkStateStale = false;
    assert(!select(s, e, p).valid); // authoritative Ready, no preview
}
