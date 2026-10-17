#include "../src/FlightDisplayHealth.h"
#include <cassert>
#include <initializer_list>
static uint32_t testClock = 0;
static uint32_t clockNow() { return testClock; }
int main() {
    using namespace FlightDisplayHealth;
    assert(pair(1, 256) == 0xff01 && clamp(65536) == 65535);
    assert(!slowFlush(49) && slowFlush(50));
    Counters h;
    h.loop(0xfffffff0); h.loop(12);
    assert(h.iterations == 2 && h.maxGap == 28);
    assert(!h.pendingAlarm(5000)); // static UI is not stalled
    h.request(100);
    h.invalidationCallback(); // raw callback count alone does not imply queued visual work
    assert(h.invalidations == 1 && h.pending);
    assert(!h.pendingAlarm(2099) && h.pendingAlarm(2100) && !h.pendingAlarm(3100));
    h.flushed(3101, 51);
    assert(h.pending && h.flushes == 1 && h.maxFlushMs == 51 && h.lastFlush == 3101);
    assert(h.slowestFlush == 51);
    assert(!h.pendingAlarm(3102)); // other partial flushes can still follow
    h.refreshReady();
    assert(!h.pending);
    h.request(4000); h.refreshReady(); // clipped/no-op refresh is not a stall
    assert(!h.pendingAlarm(7000) && h.ready == 2);
    h.request(0xfffffff0);
    assert(h.pendingAlarm(0x7e0)); // millis wrap
    Progress p;
    Observer o;
    struct Transition { Stall state, previous; uint16_t age; } events[8]{};
    unsigned count = 0;
    auto emit = [&](Stall s, Stall prev, uint16_t age) { events[count++] = {s, prev, age}; };
    p.loop(100);
    o.poll(p, 2100, emit);
    o.poll(p, 2600, emit);
    assert(count == 1 && events[0].state == ControllerLoopStall && events[0].age == 2000);
    p.loop(2700); o.poll(p, 2700, emit);
    assert(count == 2 && events[1].state == None && events[1].previous == ControllerLoopStall);
    p.serviceStart(2800); p.loop(4000);
    o.poll(p, 4800, emit);
    assert(count == 3 && events[2].state == LvglServiceStall);
    p.flushStart(4900); p.loop(6900);
    o.poll(p, 6900, emit);
    assert(count == 5 && events[3].state == None && events[4].state == DisplayFlushStall);
    p.flushDone(); p.serviceDone(); p.loop(7000);
    o.poll(p, 7000, emit);
    assert(count == 6 && events[5].state == None && events[5].previous == DisplayFlushStall);
    o.poll(p, 7100, emit); assert(count == 6); // static/no dirty progress is quiet
    // Observer captured now before core 1 published a timestamp from the future.
    for (Stall kind : {ControllerLoopStall, LvglServiceStall, DisplayFlushStall}) {
        Progress concurrent;
        concurrent.loop(3001);
        if (kind == LvglServiceStall) concurrent.serviceStart(3001);
        if (kind == DisplayFlushStall) concurrent.flushStart(3001);
        assert(concurrent.classify(3000) == None);
        assert(concurrent.age(kind, 3000) == 0);
        assert(concurrent.classify(5001) == kind);
        assert(concurrent.age(kind, 5001) == 2000);
        Progress wrapped;
        wrapped.loop(0xfffffff0);
        if (kind == LvglServiceStall) wrapped.serviceStart(0xfffffff0);
        if (kind == DisplayFlushStall) wrapped.flushStart(0xfffffff0);
        assert(wrapped.classify(0x7c0) == kind);
        assert(wrapped.age(kind, 0x7c0) == 2000);
    }
    Progress diagnostic;
    diagnostic.loop(100);
    testClock = 3000;
    // Preparation (file reads/snapshot capture) must not be hidden by output scopes.
    assert(diagnostic.classify(testClock) == ControllerLoopStall);
    {
        ScopedDiagnosticOutput scope(diagnostic, clockNow);
        assert(diagnostic.classify(7000) == None);
        diagnostic.serviceStart(3000);
        assert(diagnostic.classify(5000) == LvglServiceStall);
        diagnostic.serviceDone();
        testClock = 3500;
        {
            ScopedDiagnosticOutput nested(diagnostic, clockNow);
            assert(diagnostic.diagnosticDepth.load() == 2);
        }
        assert(diagnostic.diagnosticDepth.load() == 1);
        assert(diagnostic.classify(7000) == None); // inner end cannot re-enable loop-stall reporting
        testClock = 7000;
    }
    assert(diagnostic.diagnosticDepth.load() == 0);
    assert(diagnostic.classify(7000) == None);
    assert(diagnostic.classify(9000) == ControllerLoopStall);
    return 0;
}
