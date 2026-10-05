#include "../src/SparkOutbound.h"
#include "../src/SparkResponseLane.h"
#include "../src/SparkRetainedIntents.h"
#include "../src/SparkSubmission.h"
#include <cassert>
#include <string>
#include <vector>

struct Part { uint8_t msgNum, cmd, subcmd; };
int main() {
    SparkResponseLane lane;
    SparkOutbound<Part> outbound;
    uint8_t next = 1;
    int writes = 0;
    auto write = [&](const Part &) { ++writes; return true; };
    auto send = [&](uint8_t cmd, uint8_t sub, uint32_t now) {
        if (outbound.hasRemaining() || (cmd == 2 && lane.busy(now)) || !lane.supported(cmd, sub)) return false;
        uint8_t wire = next == 0 ? 1 : next;
        next = nextNormalSparkMessageNumber(next);
        if (!outbound.start(std::vector<Part>{{wire, cmd, sub}}, write)) return false;
        return lane.acquire(cmd, sub, wire, now);
    };
    for (uint8_t sub : {0x01, 0x10, 0x11, 0x23, 0x2a, 0x2b, 0x2f, 0x71, 0x75, 0x76, 0x78}) {
        assert(send(2, sub, 10));
        const uint8_t wire = static_cast<uint8_t>(next - 1);
        assert(!send(2, 0x01, 11) && next == nextNormalSparkMessageNumber(wire));
        assert(!lane.complete(wire, 4, sub)); // final ACK
        assert(!lane.complete(wire, 5, 1)); // intermediate ACK
        assert(!lane.complete(wire, 3, 0x38)); // unsolicited HW number
        assert(!lane.complete(static_cast<uint8_t>(wire + 1), 3, sub));
        assert(!lane.complete(wire, 3, sub == 1 ? 0x10 : 1));
        // Fragments never call complete; lane remains occupied until the
        // parser delivers the whole message.
        assert(lane.active());
        assert(lane.complete(wire, 3, sub) && !lane.complete(wire, 3, sub));
    }
    assert(!send(2, 0x72, 10) && !lane.active());
    // A failed first BLE write consumes the legacy cursor step but cannot
    // retain a response owner or multipart parts after disconnect.
    SparkOutbound<Part> failed;
    assert(!failed.start({{next, 2, 1}}, [](const Part &) { return false; }));
    assert(!lane.active() && !failed.hasRemaining());
    assert(send(2, 1, UINT32_MAX - 100));
    uint8_t publicationGate = static_cast<uint8_t>(next - 1);
    bool owned = lane.owns(publicationGate, 1);
    assert(!lane.expire(1398) && lane.expire(1399) && !lane.expire(1400));
    if (owned) publicationGate = 0;
    assert(publicationGate == 0 && !lane.complete(static_cast<uint8_t>(next - 1), 3, 1));
    uint8_t old = static_cast<uint8_t>(next - 1);
    assert(send(2, 0x01, 4900));
    assert(!lane.complete(old, 3, 1));
    uint8_t current = static_cast<uint8_t>(next - 1);
    lane.revokeControllerFullPreset(current);
    assert(!lane.active());
    assert(send(2, 0x10, 4901));
    const uint8_t activeNumber = static_cast<uint8_t>(next - 1);
    // Mutations may pass a pending response, without stealing its owner.
    assert(send(1, 0x38, 4901));
    const uint8_t afterMutation = next;
    assert(!send(2, 1, 4901) && next == afterMutation);
    assert(lane.active());
    assert(!lane.complete(current, 3, 1));
    lane.revokeControllerFullPreset(activeNumber);
    assert(lane.active()); // unrelated query survives revoke
    lane.revokeControllerFullPreset(0xEE);
    assert(lane.active());
    lane.reset(); // ingress invalidation
    assert(!lane.active() && send(2, 1, 4902));
    publicationGate = static_cast<uint8_t>(next - 1);
    owned = lane.owns(publicationGate, 1);
    lane.reset(); // ingress invalidation of full query
    if (owned) publicationGate = 0;
    assert(publicationGate == 0 && !lane.complete(static_cast<uint8_t>(next - 1), 3, 1));
    assert(send(2, 1, 4902));
    publicationGate = static_cast<uint8_t>(next - 1);
    owned = lane.owns(publicationGate, 1);
    lane.reset(); // link reset
    if (owned) publicationGate = 0;
    assert(publicationGate == 0 && !lane.complete(static_cast<uint8_t>(next - 1), 3, 1));
    assert(!lane.active());
    next = 0xED;
    assert(send(2, 1, 4903) && next == 0xEF);
    assert(lane.complete(0xED, 3, 1));
    next = 0;
    assert(send(2, 1, 4904) && next == 2);
    assert(lane.complete(1, 3, 1));
    assert(lane.acquire(2, 1, 0xEE, 4904));
    lane.revokeControllerFullPreset(0xEE); // reserved background cache fetch
    assert(lane.active() && lane.complete(0xEE, 3, 1));
    assert(send(1, 0x38, 4905) && !lane.active());
    assert(send(2, 0x10, 4906)); // NEO mutation missing final ACK
    uint8_t query = static_cast<uint8_t>(next - 1);
    assert(writeSparkProtocolAck(std::vector<Part>{{99, 4, 0x70}}, next, write));
    assert(lane.active() && !lane.complete(99, 4, 0x70));
    assert(lane.complete(query, 3, 0x10));

    SparkRetainedIntents intents;
    intents.request(SparkRetainedIntents::LooperConfig);
    intents.request(SparkRetainedIntents::LooperStatus);
    intents.request(SparkRetainedIntents::AmpName);
    intents.request(SparkRetainedIntents::Checksums);
    int attempts = 0;
    assert(!intents.service([&](auto k) { assert(k == SparkRetainedIntents::AmpName); ++attempts; return false; }));
    assert(intents.pending(SparkRetainedIntents::AmpName) && attempts == 1);
    assert(intents.service([&](auto k) { assert(k == SparkRetainedIntents::AmpName); return true; }));
    intents.request(SparkRetainedIntents::Serial);
    assert(!intents.service([&](auto k) { assert(k == SparkRetainedIntents::Serial); return false; }));
    assert(intents.pending(SparkRetainedIntents::Serial));
    assert(intents.service([&](auto k) { assert(k == SparkRetainedIntents::Serial); return true; }));
    assert(intents.service([&](auto k) { assert(k == SparkRetainedIntents::Checksums); return true; }));
    intents.request(SparkRetainedIntents::CurrentPreset);
    assert(!intents.service([&](auto k) { assert(k == SparkRetainedIntents::CurrentPreset); return false; }));
    assert(intents.pending(SparkRetainedIntents::CurrentPreset));
    assert(intents.service([&](auto k) { assert(k == SparkRetainedIntents::CurrentPreset); return true; }));
    assert(intents.service([&](auto k) { assert(k == SparkRetainedIntents::LooperConfig); return true; }));
    assert(intents.service([&](auto k) { assert(k == SparkRetainedIntents::LooperStatus); return true; }));
    intents.request(SparkRetainedIntents::LooperRecordStatus);
    assert(!intents.service([&](auto k) { assert(k == SparkRetainedIntents::LooperRecordStatus); return false; }));
    assert(intents.pending(SparkRetainedIntents::LooperRecordStatus));
    assert(intents.service([&](auto k) { assert(k == SparkRetainedIntents::LooperRecordStatus); return true; }));
    // Legacy full-preset follow-up requested during a 03/10 handler cannot
    // dispatch until that handler's response owner has retired.
    assert(lane.acquire(2, 0x10, 20, 6000));
    intents.request(SparkRetainedIntents::CurrentPreset);
    assert(!intents.service([&](auto k) {
        assert(k == SparkRetainedIntents::CurrentPreset);
        return !lane.busy(6001);
    }));
    assert(intents.pending(SparkRetainedIntents::CurrentPreset));
    assert(lane.complete(20, 3, 0x10));
    assert(intents.service([&](auto k) {
        assert(k == SparkRetainedIntents::CurrentPreset);
        return !lane.busy(6002);
    }));
    // All queued action kinds keep their pending intent on busy, not on an
    // actual attempted BLE failure. No sent timestamp is started on busy.
    for (int action = 0; action != 4; ++action) {
        bool queued = true, sent = false, failedAction = false;
        auto result = SparkSubmission::Busy;
        if (!keepQueuedSparkIntent(result)) { queued = false; sent = result == SparkSubmission::Sent; failedAction = !sent; }
        assert(queued && !sent && !failedAction);
        result = SparkSubmission::Sent;
        if (!keepQueuedSparkIntent(result)) { queued = false; sent = result == SparkSubmission::Sent; failedAction = !sent; }
        assert(!queued && sent && !failedAction);
    }
    intents.request(SparkRetainedIntents::AmpName);
    intents.reset();
    assert(!intents.pending(SparkRetainedIntents::AmpName));
    assert(keepQueuedSparkIntent(SparkSubmission::Busy));
    assert(!keepQueuedSparkIntent(SparkSubmission::Failed));
    assert(!keepQueuedSparkIntent(SparkSubmission::Sent));
    SparkRetainedIntents stopIntents;
    bool queuedStop = true;
    const auto stopped = SparkSubmission::Busy;
    if (refreshLooperAfterStop(stopped == SparkSubmission::Sent))
        stopIntents.request(SparkRetainedIntents::LooperStatus);
    if (!keepQueuedSparkIntent(stopped)) queuedStop = false;
    assert(queuedStop && !stopIntents.pending(SparkRetainedIntents::LooperStatus));
    assert(!stopIntents.service([](auto) { assert(false); return true; }));
    queuedStop = false; // owner free: controller retries the original action
    assert(!queuedStop && !stopIntents.pending(SparkRetainedIntents::LooperStatus));
    assert(refreshLooperAfterStop(true)); // later REC_COMPLETE failure may refresh
    assert(std::string(legacyLooperButtonFailure(SparkSubmission::Busy)).find("not sent. Press again") != std::string::npos);
    assert(std::string(legacyLooperButtonFailure(SparkSubmission::Failed)).find("not sent") != std::string::npos);
    // A composite button operation whose first write succeeded must not be
    // retained/replayed after its second subcommand failed.
    const bool firstButtonPartSent = true;
    const SparkSubmission secondPart = SparkSubmission::Failed;
    assert(firstButtonPartSent && !keepQueuedSparkIntent(secondPart));

    // The controller full-preset owner expires before the two-second retry
    // cadence. An earlier attempt cannot replace it or revoke its gate; busy
    // due another owner likewise preserves semantic correlation.
    SparkResponseLane retryLane;
    assert(retryLane.acquire(2, 1, 31, 100));
    uint8_t controllerGate = 31;
    uint8_t stateGate = 31;
    assert(retryLane.owns(31, 1) && !retryLane.expire(1599));
    assert(controllerGate == 31 && stateGate == 31);
    assert(!retryLane.acquire(2, 1, 32, 1599));
    assert(retryLane.expire(1600));
    if (controllerGate == 31) controllerGate = 0;
    assert(controllerGate == 0 && !retryLane.complete(31, 3, 1));
    assert(retryLane.acquire(2, 1, 32, 2100));
    controllerGate = stateGate = 32;
    assert(retryLane.complete(32, 3, 1));
    assert(retryLane.acquire(2, 0x10, 33, 2200));
    assert(!retryLane.acquire(2, 1, 34, 2200));
    assert(controllerGate == 32 && stateGate == 32); // busy, no attemptedAt
    retryLane.reset();
    assert(retryLane.acquire(2, 1, 34, 2300));
    controllerGate = stateGate = 34;
    assert(!retryLane.expire(3799));
    bool wasControllerOwner = retryLane.owns(controllerGate, 1);
    assert(retryLane.expire(3800));
    if (wasControllerOwner) controllerGate = 0;
    assert(controllerGate == 0 && !retryLane.complete(34, 3, 1));

    // Both normal controller preset queries release at 1.5 seconds across
    // millis() wrap. A busy poll cannot replace the owner before expiry;
    // an expired reply cannot complete the replacement query.
    SparkResponseLane otherLane;
    const uint32_t sentAt = UINT32_MAX - 100;
    for (uint8_t sub : {0x01, 0x10}) {
        assert(otherLane.acquire(2, sub, 40, sentAt));
        assert(!otherLane.expire(1398) && otherLane.busy(1398));
        assert(!otherLane.acquire(2, sub, 41, 1398));
        assert(otherLane.expire(1399) && !otherLane.expire(1400));
        assert(!otherLane.complete(40, 3, sub));
        assert(otherLane.acquire(2, sub, 41, 1399));
        assert(!otherLane.complete(40, 3, sub));
        assert(otherLane.complete(41, 3, sub));
    }
    // All other queries and reserved EE retain five seconds, including wrap.
    for (uint8_t sub : {0x11, 0x23, 0x2a, 0x2b, 0x2f, 0x71, 0x75, 0x76, 0x78}) {
        assert(otherLane.acquire(2, sub, 40, sentAt));
        assert(!otherLane.expire(1399) && otherLane.busy(4898));
        assert(!otherLane.acquire(2, 0x10, 41, 4898));
        assert(otherLane.expire(4899) && !otherLane.expire(4900));
    }
    for (uint8_t sub : {0x01, 0x10}) {
        assert(otherLane.acquire(2, sub, 0xEE, sentAt));
        assert(!otherLane.expire(1399) && otherLane.busy(4898));
        assert(otherLane.expire(4899));
    }
}
