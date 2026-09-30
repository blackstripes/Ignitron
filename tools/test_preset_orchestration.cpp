// Deterministic host protocol model for preset-change event ordering.
// It composes production queue, ACK, number-history and reconciliation
// primitives, but does not execute ControllerActions or SparkDataControl.
#include "controller/PresetAckMatch.h"
#include "controller/PresetTargetQueue.h"
#include "controller/PresetTimeoutReconcile.h"
#include "controller/ProtocolObservations.h"
#include <cassert>
#include <cstdint>

struct Ack { uint8_t subcmd, msg; };
struct Number { uint8_t slot, cmd, subcmd, msg; };
struct Full { uint8_t msg; };

class PresetDriver {
public:
    uint8_t confirmed = 1;
    uint8_t pending = 0;
    uint8_t sent = 0;
    uint8_t fullTarget = 0;
    unsigned sends = 0;
    unsigned confirmations = 0;
    unsigned refreshes = 0;
    unsigned failures = 0;
    PresetTargetQueue targets;
    PresetTimeoutReconcile reconcile;

    void request(uint8_t target) {
        assert(target != 0);
        // A newer intent revokes both the startup and command full-query gates.
        fullQuery = 0;
        targets.select(target, sent != 0);
        pending = target;
    }
    void ack(uint8_t subcmd, uint8_t msg) { acks.record({subcmd, msg}); }
    void number(uint8_t slot, uint8_t subcmd, uint8_t msg) {
        confirmed = slot; // Spark may update the display without confirming our send.
        numbers.record({slot, 0x03, subcmd, msg});
    }
    void full(uint8_t msg) { fulls.record({msg}); }

    void tick() {
        if (reconcile.needed()) {
            Number n{};
            while (numbers.next(reconcile.cursor(), n)) {
                if (reconcile.observe(n.slot, n.cmd, n.subcmd, n.msg, confirmed)) {
                    reconciled = n.slot;
                    return; // ControllerActions dispatches on the NEXT tick.
                }
            }
            if (!reconcile.queryOutstanding()) reconcile.startQuery(nextMessage(), numbers.revision());
            return;
        }
        if (sent) {
            Number n{};
            bool matched = false, observedTarget = false;
            while (numbers.next(numberCursor, n)) {
                if (matchesHardwareNumberReply(verificationQuery, n.cmd, n.subcmd, n.msg) && n.slot == sent)
                    matched = true;
                if (n.slot == sent) observedTarget = true;
            }
            if (matched && confirmed == sent) {
                ++confirmations;
                const uint8_t finished = sent;
                sent = 0;
                verificationQuery = 0;
                if (targets.deferred()) {
                    targets.promote();
                    if (targets.queued() == confirmed) {
                        targets.takeQueued(); // startup sync must fetch a NEW full response
                        pending = 0;
                    }
                } else {
                    pending = 0;
                    fullTarget = finished;
                    fullCursor = fulls.revision();
                    fullQuery = nextMessage();
                }
            } else {
                Ack a{};
                bool matchedAck = false;
                while (acks.next(ackCursor, a))
                    if (matchesHardwarePresetAck(switchMessage, a.subcmd, a.msg)) matchedAck = true;
                if (!verificationQuery && (matchedAck || observedTarget))
                    verificationQuery = nextMessage(); // ACK/broadcast triggers query, never confirms
            }
            return;
        }
        if (fullQuery) {
            Full f{};
            while (fulls.next(fullCursor, f))
                if (f.msg == fullQuery && confirmed == fullTarget) {
                    ++refreshes;
                    fullQuery = 0;
                    break;
                }
            return;
        }
        if (targets.queued()) {
            uint8_t target = targets.takeQueued();
            bool mustSend = reconciled != 0 && reconciled != target;
            reconciled = 0;
            if (target == confirmed && !mustSend) {
                pending = 0; // no switch sent; startup must fetch full data
            } else {
                sent = target;
                switchMessage = nextMessage();
                ackCursor = acks.revision();
                numberCursor = numbers.revision();
                verificationQuery = 0;
                ++sends;
            }
        }
    }
    void timeout() {
        assert(sent);
        ++failures;
        sent = 0;
        verificationQuery = 0;
        if (targets.deferred()) {
            targets.promote();
            reconcile.require();
            reconciled = 0;
        } else pending = 0;
    }
    uint8_t switchMsg() const { return switchMessage; }
    uint8_t queryMsg() const { return verificationQuery; }
    uint8_t reconcileMsg() const { return reconcileMessage(); }
    uint8_t fullMsg() const { return fullQuery; }

private:
    // Fixed distinct wire ids, including stale replies in each scenario.
    uint8_t nextMessage() { return ++message; }
    uint8_t reconcileMessage() const { return message; }
    uint8_t message = 10, switchMessage = 0, verificationQuery = 0, fullQuery = 0, reconciled = 0;
    uint32_t ackCursor = 0, numberCursor = 0, fullCursor = 0;
    ProtocolObservations<Ack> acks;
    ProtocolObservations<Number> numbers;
    ProtocolObservations<Full> fulls;
};

int main() {
    PresetDriver p;
    p.request(2);
    p.request(3); // replace unsent selection
    p.tick();
    assert(p.sent == 3 && p.sends == 1);
    const auto firstSwitch = p.switchMsg();
    p.request(4);
    p.request(2);
    p.request(4); // latest deferred selection wins
    p.ack(0x38, firstSwitch - 1);
    p.ack(0x15, firstSwitch);
    p.number(3, 0x38, firstSwitch);
    p.tick();
    assert(p.queryMsg() && p.confirmations == 0 && p.pending == 4);
    const auto firstQuery = p.queryMsg();
    p.number(3, 0x10, firstQuery - 1);
    p.tick();
    assert(p.sent == 3 && p.confirmations == 0);
    p.number(3, 0x10, firstQuery);
    p.tick();
    assert(p.confirmations == 1 && p.targets.queued() == 4 && p.pending == 4);
    p.tick();
    assert(p.sent == 4 && p.sends == 2);
    const auto secondSwitch = p.switchMsg();
    p.ack(0x38, firstSwitch); // late ACK for previous send
    p.tick();
    assert(p.queryMsg() == 0);
    p.ack(0x38, secondSwitch);
    p.tick();
    const auto secondQuery = p.queryMsg();
    assert(secondQuery != 0);
    p.number(4, 0x38, secondSwitch);
    p.tick();
    assert(p.confirmations == 1); // broadcast alone is not verification
    p.number(4, 0x10, firstQuery); // delayed response to old query
    p.tick();
    assert(p.confirmations == 1);
    p.number(4, 0x10, secondQuery);
    p.tick();
    assert(p.confirmations == 2 && p.pending == 0 && p.fullMsg());
    const auto obsoleteFull = p.fullMsg();
    p.request(2); // supersedes full query before it returns
    p.full(obsoleteFull);
    p.tick();
    assert(p.sent == 2 && p.refreshes == 0 && p.pending == 2);
    p.request(3);
    p.timeout();
    assert(p.failures == 1 && p.pending == 3 && p.reconcile.needed());
    p.tick(); // issue post-timeout number query
    const auto recoveryQuery = p.reconcileMsg();
    p.number(3, 0x10, secondQuery); // stale reply happens to equal latest intent
    p.tick();
    assert(p.reconcile.needed() && p.sent == 0);
    p.number(3, 0x38, recoveryQuery); // broadcast with same message is not a reply
    p.tick();
    assert(p.reconcile.needed());
    p.number(2, 0x10, recoveryQuery); // authoritative result differs from target
    p.tick();
    assert(!p.reconcile.needed() && p.targets.queued() == 3);
    p.number(3, 0x38, 0); // intervening report cannot satisfy unsent intent
    p.tick();
    assert(p.sent == 3 && p.sends == 4);
    p.ack(0x38, p.switchMsg());
    p.tick();
    const auto finalQuery = p.queryMsg();
    p.number(3, 0x10, finalQuery);
    p.tick();
    assert(p.confirmations == 3 && p.fullMsg() && p.pending == 0);
    const auto finalFull = p.fullMsg();
    p.full(obsoleteFull); // delayed full reply cannot make latest payload ready
    p.tick();
    assert(p.refreshes == 0 && p.fullMsg());
    p.full(finalFull);
    p.tick();
    assert(p.refreshes == 1 && p.fullMsg() == 0 && p.confirmed == 3);
}
