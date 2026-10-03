// Host-only model of the FX verification branch. Uses the production
// FX-specific cadence/correlation helper; does not execute BLE parsing or ControllerActions.
#include "controller/FxFullPresetRetry.h"
#include "controller/FullPresetRetry.h"
#include <cassert>
#include <cstdint>

struct FxDriver {
    FxFullPresetRetry retry;
    uint32_t sentAt = 0;
    uint32_t revision = 10;
    uint8_t expected = 0;
    bool pending = false;
    bool acked = false;
    bool enabled = true;
    bool failed = false;
    bool ready = true;
    FullPresetRetry startupRetry;
    uint8_t startupExpected = 0;

    bool selectPreset() const { return !pending; } // serialized until resolution
    bool toggle() {
        if (pending || !ready) return false;
        pending = true;
        sentAt = 100;
        retry.reset();
        acked = false;
        return true;
    }
    void ack(uint8_t subcmd, uint8_t msg) {
        if (pending && subcmd == 0x15 && msg == 7) acked = true;
    }
    bool tick(uint32_t now, bool sendSucceeds, uint8_t newMessage) {
        timeout(now);
        if (!pending || !retry.due(now, sentAt)) return false;
        expected = 0; // revoke Spark publication gate even if the send fails
        retry.attemptedAt(now, sendSucceeds, newMessage, revision, acked);
        if (sendSucceeds) expected = newMessage;
        return true;
    }
    void full(uint32_t now, uint8_t msg, bool on) {
        timeout(now);
        if (!pending) return;
        if (msg != expected || expected == 0) return; // Spark rejects obsolete reply
        ++revision;
        enabled = on;
        if (retry.matches(msg, revision)) {
            if (!on && !retry.querySentAfterAck()) return; // pre-ACK old state is inconclusive
            pending = false; // target confirms even without ACK; authoritative old state conflicts
            failed = !on;
            expected = 0;
            retry.reset();
            if (failed) invalidateForRecovery();
        }
    }
    void direct(bool on) {
        if (!pending) return;
        enabled = on;
        failed = !on;
        pending = false;
        expected = 0;
        retry.reset();
        if (failed) invalidateForRecovery();
    }
    void timeout(uint32_t now) {
        if (!pending || !retry.expired(now, sentAt)) return;
        pending = false;
        failed = true;
        expected = 0;
        retry.reset();
        invalidateForRecovery();
    }
    void invalidateForRecovery() {
        ready = false; // ControllerState::invalidatePresetData blocks new FX
        expected = 0; // Spark publication gate revoked, even after send failure
        startupExpected = 0; // ControllerState expectation revoked
        startupRetry.reset(); // next process() owns a fresh correlated query
    }
    void startupQuery(uint32_t now, bool sent, uint8_t msg) {
        assert(!pending && !ready && startupRetry.due(now));
        startupExpected = 0;
        startupRetry.attemptedAt(now, sent, msg, revision);
        if (sent) startupExpected = msg; // both gates opened for this message
    }
    void recoveryFull(uint8_t msg, bool on) {
        if (msg != startupExpected || !startupRetry.matches(msg, revision + 1)) return;
        ++revision; // only an applied, correlated payload advances the revision
        enabled = on;
        ready = true;
        startupExpected = 0;
        startupRetry.reset();
    }
    void disconnect() {
        pending = false;
        acked = false;
        expected = 0;
        retry.reset();
    }
};

int main() {
    FxDriver silent;
    assert(silent.toggle());
    assert(!silent.tick(1099, true, 40));
    assert(silent.tick(1100, true, 40) && !silent.retry.querySentAfterAck());
    assert(silent.pending); // no ACK and no direct observation
    silent.full(1101, 40, true);
    assert(!silent.pending && !silent.failed && silent.selectPreset());

    assert(silent.toggle());
    assert(silent.tick(1100, true, 41));
    silent.full(1101, 41, false); // pre-ACK old slot is not a conflict
    assert(silent.pending && !silent.failed && silent.ready);
    assert(!silent.tick(6099, true, 42));
    assert(silent.tick(6100, true, 42) && !silent.retry.querySentAfterAck());
    silent.full(6101, 41, true); // stale response after retry cannot confirm
    assert(silent.pending && silent.expected == 42);
    silent.full(6102, 42, true); // target confirms even without ACK
    assert(!silent.pending && !silent.failed);

    assert(silent.toggle());
    assert(silent.tick(1100, true, 43));
    silent.ack(0x15, 7); // ACK after first query cannot authorize it retroactively
    silent.full(1101, 43, false);
    assert(silent.pending && !silent.failed && !silent.retry.querySentAfterAck());
    assert(silent.tick(6100, true, 44) && silent.retry.querySentAfterAck());
    silent.full(6101, 44, false); // post-ACK query is authoritative
    assert(!silent.pending && silent.failed && !silent.ready);

    FxDriver noAckDeadline;
    assert(noAckDeadline.toggle());
    assert(noAckDeadline.tick(1100, true, 45));
    assert(noAckDeadline.tick(6100, true, 46));
    assert(noAckDeadline.tick(11100, true, 47));
    noAckDeadline.full(15099, 46, true); // obsolete reply cannot extend deadline
    assert(noAckDeadline.pending);
    noAckDeadline.full(15100, 47, true);
    assert(noAckDeadline.failed && !noAckDeadline.ready && !noAckDeadline.pending);
    assert(!noAckDeadline.tick(16100, true, 48));

    FxDriver fx;
    assert(fx.toggle() && !fx.selectPreset() && !fx.toggle());
    fx.ack(0x15, 6);
    assert(!fx.tick(110, false, 20)); // grace period, irrespective of ACK
    fx.ack(0x15, 7);
    assert(!fx.tick(1099, false, 20));
    assert(fx.tick(1100, false, 20)); // failed initial send consumes schedule
    assert(fx.retry.attempted() && !fx.retry.queryDispatched());
    assert(fx.expected == 0 && fx.pending && !fx.selectPreset());
    assert(!fx.tick(6099, true, 21));
    const bool firstSuccessfulQueryWasRetry = fx.retry.queryDispatched();
    assert(fx.tick(6100, true, 21));
    assert(!firstSuccessfulQueryWasRetry && fx.retry.queryDispatched());
    assert(fx.expected == 21 && !fx.selectPreset());
    // Partial/malformed replies never reach the applied full observation gate.
    assert(!fx.tick(8120, true, 22) && fx.pending && fx.expected == 21);
    assert(!fx.tick(11099, true, 22)); // full response still has its window
    assert(fx.tick(11100, true, 22)); // unanswered reply expires; revoke 21
    fx.full(11101, 21, true); // late complete old reply cannot publish/confirm
    assert(fx.pending && fx.expected == 22 && fx.revision == 10 && !fx.selectPreset());
    fx.full(11102, 22, true);
    assert(!fx.pending && !fx.failed && fx.selectPreset());

    assert(fx.toggle());
    fx.ack(0x15, 7);
    assert(fx.tick(1100, true, 23));
    fx.full(1101, 23, false); // post-ACK full preset conflicts
    assert(fx.failed && !fx.pending && !fx.ready && !fx.toggle());
    fx.recoveryFull(23, true); // obsolete verification reply rejected
    assert(!fx.ready && !fx.toggle());
    fx.startupQuery(200, false, 26); // failed send never opens a gate
    fx.recoveryFull(26, true);
    assert(!fx.ready && !fx.toggle());
    assert(!fx.startupRetry.due(2199));
    fx.startupQuery(2200, true, 28);
    fx.recoveryFull(23, true);
    assert(!fx.ready && !fx.toggle());
    fx.recoveryFull(28, false); // authoritative conflicting state restored
    assert(fx.ready && !fx.enabled && fx.toggle());
    fx.direct(true);

    assert(fx.toggle());
    fx.direct(true); // direct FX_ONOFF does not need an ACK or full query
    assert(!fx.pending && !fx.failed && !fx.retry.attempted());

    assert(fx.toggle());
    fx.ack(0x15, 7);
    assert(fx.tick(1100, true, 24));
    fx.direct(true); // fresh FX_ONOFF resolves without a full reply
    assert(!fx.pending && fx.expected == 0 && !fx.failed);

    assert(fx.toggle());
    fx.ack(0x15, 7);
    assert(fx.tick(1100, true, 25));
    assert(!fx.tick(6099, false, 26));
    assert(fx.tick(6100, false, 26)); // failed replacement revokes old gate
    assert(!fx.retry.querySentAfterAck());
    fx.full(6101, 25, true);
    assert(fx.pending && !fx.selectPreset());
    assert(!fx.tick(11099, true, 27));
    assert(fx.tick(11100, true, 27));
    assert(!fx.tick(15099, true, 28)); // no extra query at the deadline
    fx.full(15100, 27, true); // even a matching reply at the deadline is too late
    assert(fx.failed && !fx.ready && !fx.retry.attempted() && !fx.toggle());
    assert(!fx.tick(20110, true, 28));
    fx.startupQuery(15101, true, 29);
    fx.recoveryFull(27, false);
    assert(!fx.ready && !fx.toggle());
    fx.recoveryFull(29, true);
    assert(fx.ready && fx.toggle());
    fx.direct(true);

    assert(fx.toggle());
    fx.ack(0x15, 7);
    assert(fx.tick(1100, true, 27));
    fx.disconnect();
    fx.full(111, 27, true);
    assert(fx.selectPreset() && !fx.retry.attempted() && fx.expected == 0);
    assert(fx.toggle() && !fx.retry.attempted()); // new operation starts clean

    // The interval/deadline remain correct across millis() wrap.
    FxFullPresetRetry wrapped;
    const uint32_t sent = UINT32_MAX - 1000;
    assert(!wrapped.due(sent + 999, sent));
    assert(wrapped.due(sent + 1000, sent));
    wrapped.attemptedAt(sent + 1000, true, 30, 4, false);
    assert(!wrapped.querySentAfterAck());
    assert(!wrapped.due(sent + 5999, sent));
    assert(wrapped.due(sent + 6000, sent));
    assert(!wrapped.expired(sent + 14999, sent));
    assert(wrapped.expired(sent + 15000, sent));
    assert(!wrapped.due(sent + 15000, sent));
}
