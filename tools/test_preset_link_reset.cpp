#include "controller/PresetLinkReset.h"
#include <cassert>

int main() {
    PresetTargetQueue targets;
    PresetTimeoutReconcile reconcile;
    targets.select(2, false);
    assert(targets.takeQueued() == 2); // sent on old link
    targets.select(3, true); // deferred latest intent
    reconcile.require();
    reconcile.startQuery(43, 12);
    assert(reconcile.needed() && reconcile.queryOutstanding());
    resetPresetLinkPending(targets, reconcile);
    assert(targets.queued() == 0 && targets.deferred() == 0);
    assert(!reconcile.needed() && !reconcile.queryOutstanding() && reconcile.cursor() == 0);
    assert(!reconcile.observe(3, 3, 0x10, 43, 3)); // old reply cannot release gate

    targets.select(4, false); // unsent work is discarded too
    reconcile.require(); // query not yet issued
    resetPresetLinkPending(targets, reconcile);
    resetPresetLinkPending(targets, reconcile); // disconnected ticks are idempotent
    assert(targets.queued() == 0 && !reconcile.needed());
}
