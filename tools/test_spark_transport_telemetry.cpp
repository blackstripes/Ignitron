#include "../src/SparkTransportTelemetry.h"
#include "../src/SparkResponseLane.h"
#include "../src/SparkOutbound.h"
#include <cassert>

int main() {
    SparkTransportTelemetry t;
    auto q = t.begin(1, 0xfffffff0);
    t.start(q, 4); // wrap-safe queue delay
    t.write(q, 20, 3, true);
    t.notification(q, 40);
    t.notification(q, 42);
    t.response(q, 210);
    t.response(q, 211);
    assert(t.get(q)->firstNotification == 40 && t.get(q)->parsed == 210);
    assert(t.queueDelay.count[0] == 1 && t.firstResponse.count[0] == 1);
    assert(t.completeResponse.count[1] == 1 && t.responses == 1);
    auto m = t.begin(2, 300);
    t.start(m, 302);
    t.write(m, 305, 1, true);
    t.semantic(m, 5400);
    assert(t.confirmation.count[4] == 1 && t.get(m)->reason == SparkTransportTelemetry::Reason::Confirmed);
    auto lost = t.begin(1, 10);
    t.start(lost, 11);
    t.write(lost, 12, 2, true);
    t.retry(lost);
    t.ingressInvalidation(lost);
    t.end(lost, SparkTransportTelemetry::Reason::Timeout);
    assert(t.invalidations == 1 && t.timeouts == 0 && t.get(lost)->invalidations == 1);
    // A second parser reset without an owner is still one ingress invalidation.
    t.ingressInvalidation(0);
    assert(t.invalidations == 2 && t.get(lost)->invalidations == 1);
    auto expired = t.begin(1, 20);
    t.end(expired, SparkTransportTelemetry::Reason::Timeout);
    t.end(expired, SparkTransportTelemetry::Reason::Timeout);
    assert(t.timeouts == 1 && t.retries == 1);
    auto failed = t.begin(2, 22);
    t.start(failed, 23);
    t.write(failed, 25, 2, false);
    assert(t.writeFailed == 1 && !t.get(failed)->written);
    // The first notification may arrive during the BLE write. A new query
    // must use its own callback timestamp even if the old lane has expired.
    SparkResponseLane lane;
    auto prior = t.begin(1, 100);
    t.start(prior, 101);
    t.write(prior, 102, 1, true);
    assert(lane.acquire(0x02, 0x10, 3, 102));
    t.notification(prior, 110);
    assert(lane.expire(5102));
    t.end(prior, SparkTransportTelemetry::Reason::Timeout);
    auto replacement = t.begin(1, 5103);
    t.start(replacement, 5103);
    t.write(replacement, 5110, 1, true);
    assert(lane.acquire(0x02, 0x10, 4, 5110));
    t.notification(replacement, 5105); // captured while writeBLE was running
    t.retry(replacement);
    assert(t.get(prior)->retries == 0 && t.get(replacement)->retries == 1);
    assert(t.retries == 2 && t.get(replacement)->firstNotification == 5105);
    assert(t.get(replacement)->notified && t.firstResponse.count[0] == 2);
    t.notification(replacement, 5120);
    assert(t.get(replacement)->firstNotification == 5105);
    auto pendingAction = t.begin(2, 5200);
    t.start(pendingAction, 5201);
    t.write(pendingAction, 5202, 1, true);
    t.pinSemantic(pendingAction, 0);
    for (unsigned i = 0; i < SparkTransportTelemetry::capacity; ++i) t.begin(1, i);
    assert(t.get(q) == nullptr && t.get(pendingAction) == nullptr && t.get(t.lastId())->id == t.lastId());
    t.semantic(pendingAction, 10302);
    t.semantic(pendingAction, 10303); // no duplicate after ring overwrite
    assert(t.confirms == 2 && t.confirmation.count[4] == 2);
    auto failedAction = t.begin(2, 11000);
    t.write(failedAction, 11001, 1, true);
    t.pinSemantic(failedAction, 1);
    for (unsigned i = 0; i < SparkTransportTelemetry::capacity; ++i) t.begin(1, i);
    t.end(failedAction, SparkTransportTelemetry::Reason::Failed);
    t.semantic(failedAction, 16000);
    assert(t.confirms == 2);

    // sendNext calls the same writer for the first part and, after 05/01,
    // for later parts. A successful first part is not a successful command.
    struct Part { uint8_t cmd, subcmd, msgNum; };
    struct Ack { uint8_t cmd, subcmd, msgNum; };
    SparkOutbound<Part> outbound;
    SparkTransportTelemetry multipart;
    auto upload = multipart.begin(2, 100);
    unsigned writes = 0;
    auto writePart = [&](const Part &) {
        ++writes;
        const bool ok = writes == 1;
        multipart.start(upload, 101);
        multipart.write(upload, 100 + writes, writes, ok, outbound.writingLastPart());
        return ok;
    };
    assert(outbound.start({{1, 1, 7}, {1, 1, 7}}, writePart));
    assert(outbound.hasRemaining() && multipart.sent == 0);
    assert(!multipart.get(upload)->written && multipart.get(upload)->chunks == 1);
    assert(!outbound.onIntermediateAck(Ack{5, 1, 7}, writePart));
    assert(!outbound.hasRemaining() && multipart.sent == 0 && multipart.writeFailed == 1);
    assert(multipart.get(upload)->reason == SparkTransportTelemetry::Reason::WriteFailed);
    assert(!multipart.get(upload)->written && multipart.get(upload)->chunks == 3);
    assert(multipart.get(upload)->sendStart == 101 && multipart.get(upload)->sendEnd == 102);
    auto uploadOk = multipart.begin(2, 200);
    SparkOutbound<Part> okOutbound;
    auto okPart = [&](const Part &) {
        multipart.start(uploadOk, 201);
        multipart.write(uploadOk, okOutbound.writingLastPart() ? 204 : 202, 2,
                        true, okOutbound.writingLastPart());
        return true;
    };
    assert(okOutbound.start({{1, 1, 8}, {1, 1, 8}}, okPart));
    assert(multipart.sent == 0);
    assert(okOutbound.onIntermediateAck(Ack{5, 1, 8}, okPart));
    assert(multipart.sent == 1 && multipart.get(uploadOk)->chunks == 4);
    assert(multipart.get(uploadOk)->written && multipart.get(uploadOk)->sendEnd == 204);

    SparkTransportTelemetry wrapped;
    auto owner = wrapped.begin(1, 10);
    wrapped.start(owner, 11);
    wrapped.write(owner, 12, 1, true);
    for (unsigned i = 0; i < SparkTransportTelemetry::capacity; ++i)
        wrapped.begin(2, 20 + i);
    assert(wrapped.get(owner) == nullptr); // diagnostics ring remains bounded
    wrapped.retry(owner);
    wrapped.end(owner, SparkTransportTelemetry::Reason::Timeout);
    wrapped.end(owner, SparkTransportTelemetry::Reason::Timeout);
    assert(wrapped.retries == 1 && wrapped.timeouts == 1);
    auto nextOwner = wrapped.begin(1, 40);
    wrapped.start(nextOwner, 41);
    wrapped.write(nextOwner, 42, 1, true);
    for (unsigned i = 0; i < SparkTransportTelemetry::capacity; ++i)
        wrapped.begin(2, 50 + i);
    wrapped.notification(nextOwner, 50);
    wrapped.response(nextOwner, 60);
    assert(wrapped.responses == 1 && wrapped.firstResponse.count[0] == 1);
    assert(wrapped.completeResponse.count[0] == 1);
}
