// Host-only model of the FX send/ACK cursor boundary in ControllerActions.
// Uses production receive history; it does not execute BLE or ControllerActions.
#include "controller/ProtocolObservations.h"
#include <cassert>
#include <cstdint>

struct Ack { uint8_t subcmd, msgNum; };

class FxAckDriver {
public:
    void receive(uint8_t subcmd, uint8_t msgNum) { acks_.record({subcmd, msgNum}); }

    template <typename Send>
    bool send(Send sendCommand) {
        // Model the queued FX send block: the command may synchronously
        // deliver an ACK before it returns its wire message number.
        const uint32_t beforeSend = acks_.revision();
        uint8_t messageNumber = 0;
        if (!sendCommand(messageNumber)) return false;
        cursor_ = beforeSend;
        sentMessage_ = messageNumber;
        acked_ = false;
        return true;
    }

    bool poll() {
        Ack ack{};
        while (acks_.next(cursor_, ack))
            if (ack.subcmd == 0x15 && ack.msgNum == sentMessage_) acked_ = true;
        return acked_; // only starts verification; does not confirm FX state
    }

private:
    ProtocolObservations<Ack> acks_;
    uint32_t cursor_ = 0;
    uint8_t sentMessage_ = 0;
    bool acked_ = false;
};

int main() {
    FxAckDriver fx;
    // Even a stale matching wire number must not start verification.
    fx.receive(0x15, 7);
    fx.receive(0x15, 6);
    assert(fx.send([](uint8_t &msg) { msg = 7; return true; }));
    assert(!fx.poll());

    // A matching ACK arrives *inside* the send, followed by unrelated noise.
    assert(fx.send([&fx](uint8_t &msg) {
        msg = 8;
        fx.receive(0x15, 8);
        fx.receive(0x38, 8);
        return true;
    }));
    assert(fx.poll());

    FxAckDriver unrelated;
    unrelated.receive(0x15, 9); // pre-existing unrelated ACK
    assert(unrelated.send([](uint8_t &msg) { msg = 10; return true; }));
    assert(!unrelated.poll());
    unrelated.receive(0x15, 9); // late ACK from a different command
    assert(!unrelated.poll());
    unrelated.receive(0x15, 10);
    assert(unrelated.poll());
}
