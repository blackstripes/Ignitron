#include "controller/PresetTargetQueue.h"
#include "controller/PresetTimeoutReconcile.h"
#include "controller/PresetAckMatch.h"
#include <cassert>

struct Number { uint8_t slot, cmd, subcmd, msg; };

int main() {
    ProtocolObservations<Number> wire;
    PresetTargetQueue targets;
    PresetTimeoutReconcile reconcile;
    uint8_t pending = 2;
    targets.select(2, false);
    assert(targets.takeQueued() == 2); // command 2 sent
    targets.select(4, true);
    targets.select(3, true); // newest accepted intent replaces 4
    pending = 3;

    // Command 2 times out: fail only that send, NOT pending intent 3.
    assert(targets.promote() == 3);
    reconcile.require();
    assert(pending == targets.queued());
    assert(!matchesHardwarePresetAck(0, 0x38, 2));
    wire.record({2, 3, 0x10, 21}); // late reply for old verification query
    wire.record({3, 3, 0x38, 0}); // unsolicited report of desired slot
    uint32_t before = wire.revision();
    reconcile.startQuery(43, before); // post-timeout query
    Number n{};
    while (wire.next(reconcile.cursor(), n))
        assert(!reconcile.observe(n.slot, n.cmd, n.subcmd, n.msg, n.slot));
    wire.record({2, 3, 0x10, 21}); // late old reply after new query
    wire.record({3, 3, 0x10, 21}); // even desired number, wrong message
    wire.record({3, 3, 0x38, 43}); // broadcast with matching message
    while (wire.next(reconcile.cursor(), n))
        assert(!reconcile.observe(n.slot, n.cmd, n.subcmd, n.msg, n.slot));
    assert(reconcile.needed() && targets.queued() == 3 && pending == 3);

    // A query result must agree with the refreshed controller snapshot.
    wire.record({2, 3, 0x10, 43});
    assert(wire.next(reconcile.cursor(), n));
    assert(!reconcile.observe(n.slot, n.cmd, n.subcmd, n.msg, 3));
    wire.record({2, 3, 0x10, 43});
    assert(wire.next(reconcile.cursor(), n));
    assert(reconcile.observe(n.slot, n.cmd, n.subcmd, n.msg, 2));
    assert(!reconcile.needed());
    // A late unsolicited report of 3 between reconciliation and dispatch
    // cannot turn an unsent target into a confirmation: query said slot 2.
    const uint8_t reconciledNumber = 2;
    const uint8_t lateSnapshot = 3;
    assert(targets.queued() == lateSnapshot && targets.queued() != reconciledNumber);
    assert(targets.takeQueued() == 3); // now safe to send latest, NOT confirm old 2
    assert(pending == 3);

    // Retry revokes a previous query, including a reply arriving late.
    reconcile.require();
    reconcile.startQuery(44, wire.revision());
    reconcile.require();
    wire.record({3, 3, 0x10, 44});
    assert(wire.next(reconcile.cursor(), n));
    assert(!reconcile.observe(n.slot, n.cmd, n.subcmd, n.msg, 3));
}
