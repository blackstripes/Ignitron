// Host model of ControllerActions' FX observation/correlation gates. The
// production confirmation sequence itself is exercised via FxConfirmation.h.
#include "controller/FxConfirmation.h"
#include "controller/FxFullPresetRetry.h"
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <string>

struct FxConfirmationDriver {
    FxFullPresetRetry retry;
    bool pending = true;
    bool controllerPending = true;
    bool confirmed = false;
    bool enabledBefore = false;
    bool desired = true;
    bool known = true;
    bool chainMatches = true;
    bool modelMatches = true;
    bool enabled = false;
    bool acked = false;
    uint32_t modelRevision = 10;
    uint32_t modelRevisionBeforeSend = 10;
    uint32_t fullRevision = 20;
    uint32_t fullRevisionBeforeSend = 20;
    unsigned confirmations = 0;
    unsigned persistentRecords = 0;
    unsigned clears = 0;
    unsigned stateRevisions = 0;
    uint8_t sentMessage = 7;
    uint8_t observedMessage = 0;
    std::string trace;

    void ack(uint8_t subcmd, uint8_t msg) {
        if (pending && subcmd == 0x15 && msg == sentMessage) acked = true;
    }
    void query(uint8_t msg) { retry.attemptedAt(1100, true, msg, fullRevision, acked); }
    void full(uint8_t msg, bool on) {
        // Only an applied, correlated Spark response can update this snapshot.
        if (msg != retry.messageNumber()) return;
        observedMessage = msg;
        ++fullRevision;
        enabled = on;
    }
    void direct(bool on) { ++modelRevision; enabled = on; }

    template <typename Logger>
    void tick(Logger logger) {
        if (!pending) return;
        const bool fullObserved = fullRevision != fullRevisionBeforeSend;
        const bool matchingFull = fullObserved && retry.matches(observedMessage, fullRevision);
        if (fullObserved && (!known || !modelMatches || !chainMatches)) return; // cancelled
        const bool modelObserved = modelRevision != modelRevisionBeforeSend;
        const bool directSuccess = modelObserved && enabled == desired && enabled != enabledBefore;
        // A fresh conflicting FX_ONOFF takes priority over the full reply.
        if (!directSuccess && modelObserved && known && modelMatches && enabled != desired) return;
        const bool fallbackSuccess = matchingFull && known && modelMatches &&
                                     enabled == desired && enabled != enabledBefore;
        if (!directSuccess && !fallbackSuccess) return; // ACK alone is not success
        const std::string source = directSuccess ? "FX_ONOFF" : "full_preset";
        const uint8_t originalMessage = sentMessage;
        confirmFxRequest(
            [&] { controllerPending = false; confirmed = true; ++stateRevisions; },
            [&] { ++confirmations; },
            [&] { ++persistentRecords; },
            [&] { pending = false; ++clears; sentMessage = 0; retry.reset(); },
            [&] {
                assert(!pending && !controllerPending && confirmed);
                assert(confirmations == 1 && persistentRecords == 1 && clears == 1 && stateRevisions == 1);
                trace = "event=fx_confirmed slot=2 msg=" + std::to_string(originalMessage) + " source=" + source;
                logger();
            });
    }
    void tick() { tick([] {}); }
    void assertOnce(const char *source) const {
        assert(!pending && !controllerPending && confirmed);
        assert(confirmations == 1 && persistentRecords == 1 && clears == 1 && stateRevisions == 1);
        assert(trace == std::string("event=fx_confirmed slot=2 msg=7 source=") + source);
    }
};

int main() {
    FxConfirmationDriver direct;
    direct.ack(0x15, 7);
    direct.tick(); // ACK alone
    assert(direct.pending && direct.confirmations == 0);
    direct.direct(true);
    bool loggerRan = false;
    try {
        direct.tick([&] { loggerRan = true; throw std::runtime_error("serial stalled"); });
        assert(false);
    } catch (const std::runtime_error &) {}
    assert(loggerRan);
    direct.assertOnce("FX_ONOFF");
    direct.tick();
    direct.assertOnce("FX_ONOFF");

    FxConfirmationDriver fallback;
    fallback.query(40); // no ACK required for target-state full response
    fallback.full(39, true); // stale response
    fallback.tick();
    assert(fallback.pending && fallback.confirmations == 0);
    fallback.full(40, true);
    bool fallbackLoggerRan = false;
    try {
        fallback.tick([&] { fallbackLoggerRan = true; throw std::runtime_error("serial stalled"); });
        assert(false);
    } catch (const std::runtime_error &) {}
    assert(fallbackLoggerRan);
    fallback.assertOnce("full_preset");
    fallback.tick();
    fallback.assertOnce("full_preset");

    FxConfirmationDriver conflict;
    conflict.query(41);
    conflict.ack(0x15, 7); // later ACK cannot authorize an earlier query
    conflict.full(41, false);
    conflict.tick();
    assert(conflict.pending && conflict.confirmations == 0);
    conflict.query(42);
    conflict.full(41, true); // superseded response
    conflict.tick();
    assert(conflict.pending && conflict.confirmations == 0);
    conflict.full(42, false); // post-ACK conflicting response
    conflict.tick();
    assert(conflict.pending && conflict.confirmations == 0);

    FxConfirmationDriver directConflict;
    directConflict.direct(false);
    directConflict.tick();
    assert(directConflict.pending && directConflict.confirmations == 0);
}
