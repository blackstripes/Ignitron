#include "../src/AmpBatteryPoll.h"
#include "../src/SparkResponseLane.h"
#include <cassert>
#include <vector>

int main() {
    AmpBatteryPoll poll;
    SparkResponseLane lane;
    uint32_t lastAmpBatteryUpdate = 0;
    uint32_t now = 100;
    bool connected = true, remaining = false, writeOk = true, foregroundReady = true;
    SparkSubmission status = SparkSubmission::Failed;
    uint8_t next = 1;
    int writes = 0;
    std::vector<AmpBatteryPoll::Event> events;
    std::vector<AmpBatteryPoll::Transport> observations;
    auto snapshot = [&]() -> AmpBatteryPoll::Transport {
        return {status, remaining, remaining ? 2 : 0, lane.active(),
                lane.traceMessageNumber(), lane.traceSubcommand(), false};
    };
    auto send = [&]() {
        ++writes;
        if (!writeOk) { status = SparkSubmission::Failed; return false; }
        status = SparkSubmission::Sent;
        return lane.acquire(2, 0x71, next++, now);
    };
    auto trace = [&](AmpBatteryPoll::Event e, const AmpBatteryPoll::Transport &t) {
        events.push_back(e); observations.push_back(t);
    };
    auto service = [&]() { poll.service(now, connected, foregroundReady, lastAmpBatteryUpdate, snapshot, send, trace); };
    service(); // due and idle
    assert(writes == 1 && lastAmpBatteryUpdate == 100 && lane.owns(1, 0x71));
    assert((events == std::vector<AmpBatteryPoll::Event>{AmpBatteryPoll::Event::Due, AmpBatteryPoll::Event::Sent}));
    service();
    assert(writes == 1 && events.size() == 2);
    assert(lane.complete(1, 3, 0x71));

    now = 60200;
    // Foreground full query takes the lane earlier in the same scheduling cycle.
    assert(lane.acquire(2, 0x01, 42, now));
    service(); service();
    assert(writes == 1 && lastAmpBatteryUpdate == 100 && events.size() == 4);
    assert(events[2] == AmpBatteryPoll::Event::Due && events[3] == AmpBatteryPoll::Event::Deferred);
    assert(observations[3].ownerMessage == 42 && observations[3].ownerSubcommand == 1 &&
           observations[3].laneActive && !observations[3].remaining);
    remaining = true;
    service(); service();
    assert(events.size() == 5); // changed unsent state, then deduplicated
    assert(observations[4].remaining && observations[4].remainingParts == 2);
    remaining = false;
    assert(lane.complete(42, 3, 1));
    writeOk = false;
    service(); service();
    assert(writes == 3 && lastAmpBatteryUpdate == 100 && events.size() == 6);
    assert(observations[5].submission == SparkSubmission::Failed && !observations[5].laneActive);
    writeOk = true;
    service();
    assert(writes == 4 && lastAmpBatteryUpdate == now && events.back() == AmpBatteryPoll::Event::Sent);
    assert(lane.complete(2, 3, 0x71));

    now += 60001;
    connected = false;
    service(); service();
    assert(writes == 4 && lastAmpBatteryUpdate == 60200 && events.back() == AmpBatteryPoll::Event::Due);
    poll.reset(); lastAmpBatteryUpdate = 0; // resetStatus / reconnect
    service();
    assert(writes == 4 && lastAmpBatteryUpdate == 0);
    connected = true;
    service();
    assert(writes == 5 && lastAmpBatteryUpdate == now);
    assert(events.back() == AmpBatteryPoll::Event::Sent);
    assert(lane.complete(3, 3, 0x71));

    // A timed-out full query releases the lane, but its retry is still pending
    // in ControllerActions until the next loop. An idle transport is not enough.
    // An FX full-query retry also holds foregroundReady false when the preset
    // snapshot remains Ready, so the same idle-lane gap is covered here.
    now += 60001;
    foregroundReady = false;
    service(); service();
    assert(!lane.active() && writes == 5 && lastAmpBatteryUpdate != now);
    assert(events[events.size() - 2] == AmpBatteryPoll::Event::Due);
    assert(events.back() == AmpBatteryPoll::Event::Deferred);
    assert(observations.back().controllerWorkPending && !observations.back().laneActive);
    foregroundReady = true;
    service();
    assert(writes == 6 && lastAmpBatteryUpdate == now && events.back() == AmpBatteryPoll::Event::Sent);
}
