#include "controller/PresetTargetQueue.h"
#include "controller/PresetRequestGate.h"
#include <cassert>

int main() {
    PresetTargetQueue queue;
    queue.select(2, false);
    queue.select(3, false); // replace unsent
    assert(queue.takeQueued() == 3);
    assert(queue.queued() == 0);
    queue.select(4, true);
    queue.select(5, true); // only the latest intent survives
    assert(queue.deferred() == 5);
    assert(queue.promote() == 5);
    assert(queue.deferred() == 0);
    assert(queue.takeQueued() == 5);
    queue.select(6, true);
    queue.clear(); // link loss / unknowable outcome
    assert(queue.deferred() == 0 && queue.queued() == 0);

    // A deferred latest target can equal the newly confirmed slot. It gets
    // consumed without a new switch command; startup sync then issues a full
    // query and leaves the snapshot Syncing. A tap during that query is busy,
    // not an idle-phase rejection, and replaces the obsolete refresh.
    queue.select(2, false);
    assert(queue.takeQueued() == 2);
    queue.select(3, true);
    queue.select(4, true);
    queue.select(2, true);
    assert(queue.promote() == 2);
    assert(queue.takeQueued() == 2); // observed Spark slot 2
    assert(!presetSelectionMayProceedDuringSync(false, false, false, false, false));
    assert(presetSelectionMayProceedDuringSync(false, false, false, true, false));
    assert(!presetSelectionMayProceedDuringSync(false, false, false, true, true));
    queue.select(3, false); // tap during startup full query
    queue.select(2, false); // latest wins before dispatch
    assert(queue.takeQueued() == 2);
    assert(presetSelectionMayProceedDuringSync(false, false, true, false, false));
}
