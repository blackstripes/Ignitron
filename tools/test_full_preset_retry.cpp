// Host-only model of ControllerActions' post-switch -> startup full-read gate.
#include "controller/FullPresetRetry.h"
#include "controller/PresetRequestGate.h"
#include <cassert>
#include <cstdint>

int main() {
    FullPresetRetry retry;
    uint8_t confirmed = 3; // Number confirmation is already complete.
    bool ready = false;
    assert(retry.due(100));
    retry.attemptedAt(100, true, 18, 7);
    assert(!retry.matches(18, 7)); // Pre-send revision is not a reply.
    assert(!retry.due(2099));
    assert(retry.due(2100));
    // ControllerActions revokes both publication gates before the next send.
    retry.revoke();
    assert(!retry.matches(18, 8));
    retry.attemptedAt(2100, true, 19, 8);
    assert(!retry.matches(18, 9)); // Late response to expired attempt.
    assert(!retry.matches(19, 8));
    assert(!retry.due(4099) && retry.due(4100));
    retry.revoke();
    retry.attemptedAt(4100, true, 20, 8);
    assert(!retry.matches(19, 9));
    assert(retry.due(6100));
    retry.revoke();
    retry.attemptedAt(6100, true, 21, 8);
    assert(!retry.matches(20, 9));
    assert(retry.due(8100));
    retry.revoke();
    retry.attemptedAt(8100, true, 22, 8);
    assert(!retry.matches(21, 9) && retry.matches(22, 9));
    ready = retry.matches(22, 9) && confirmed == 3;
    assert(ready && confirmed == 3);
    retry.reset();
    assert(!retry.matches(22, 10));

    // A failed send still waits a bounded interval; it cannot open a gate.
    retry.attemptedAt(9000, false, 23, 10);
    assert(!retry.matches(23, 11) && !retry.due(10999));
    // A failed send still leaves an active retry schedule; a newer selection
    // must be allowed to supersede the stale refresh while the snapshot syncs.
    assert(presetSelectionMayProceedDuringSync(false, false, retry.attempted(), false, false));
    assert(retry.due(11000));
    retry.attemptedAt(11000, true, 24, 11);
    assert(!retry.matches(23, 12) && retry.matches(24, 12));
    retry.reset();
    assert(retry.due(11001)); // A new selection starts a new schedule.

    // millis() wrap must not defer a retry indefinitely.
    retry.attemptedAt(UINT32_MAX - 999, true, 25, 12);
    assert(!retry.due(999));
    assert(retry.due(1000));
}
