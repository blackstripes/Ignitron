#pragma once

#include <cstdint>

// One complete, parsed 03 reply owns the response lane. Mutations never own it:
// in particular 01/38 must allow the controller's 02/10 verification without
// waiting for a final ACK that some amps do not send.
class SparkResponseLane {
public:
    static constexpr uint32_t deadlineMs = 5000;
    static constexpr uint32_t controllerFullPresetDeadlineMs = 1500;
    static constexpr uint32_t hardwareNumberDeadlineMs = 1500;

    static bool supported(uint8_t cmd, uint8_t subcmd) {
        if (cmd != 0x02) return true;
        switch (subcmd) {
        case 0x01: case 0x10: case 0x11: case 0x23: case 0x2a:
        case 0x2b: case 0x2f: case 0x71: case 0x75: case 0x76:
        case 0x78: return true;
        default: return false; // No known completion identity: fail closed.
        }
    }

    bool busy(uint32_t now) { expire(now); return active_; }
    bool expire(uint32_t now) {
        // EE is the reserved background slot read, not a normal controller
        // query. Keep its original window along with all other query types.
        const uint32_t limit = msgNum_ == 0xEE ? deadlineMs
                               : subcmd_ == 0x01 ? controllerFullPresetDeadlineMs
                               : subcmd_ == 0x10 ? hardwareNumberDeadlineMs
                                                 : deadlineMs;
        if (!active_ || static_cast<uint32_t>(now - sentAt_) < limit) return false;
        reset();
        return true;
    }
    // Call only after the first BLE write succeeded. msgNum is the actual wire
    // number (zero is encoded as one); the caller does not reserve a number.
    bool acquire(uint8_t cmd, uint8_t subcmd, uint8_t msgNum, uint32_t now) {
        if (!supported(cmd, subcmd)) return false;
        if (cmd == 0x02) {
            if (busy(now)) return false;
            active_ = true;
            subcmd_ = subcmd;
            msgNum_ = msgNum == 0 ? 1 : msgNum;
            sentAt_ = now;
        }
        return true;
    }
    // Only a complete parser event is passed here, AFTER state handling.
    bool complete(uint8_t msgNum, uint8_t cmd, uint8_t subcmd) {
        if (!active_ || msgNum != msgNum_ || cmd != 0x03 || subcmd != subcmd_) return false;
        reset();
        return true;
    }
    void revokeControllerFullPreset(uint8_t msgNum) {
        if (active_ && subcmd_ == 0x01 && msgNum_ == msgNum && msgNum != 0xEE) reset();
    }
    void reset() { active_ = false; }
    bool active() const { return active_; }
    bool owns(uint8_t msgNum, uint8_t subcmd) const {
        return active_ && msgNum_ == msgNum && subcmd_ == subcmd;
    }

private:
    bool active_ = false;
    uint8_t msgNum_ = 0, subcmd_ = 0;
    uint32_t sentAt_ = 0;
};
