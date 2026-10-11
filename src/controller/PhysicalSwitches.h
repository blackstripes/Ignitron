#pragma once
#include <cstdint>

// One independent integrator per key. A fresh boot/read-error epoch requires
// an all-released sample before it can create any new press events.
class PhysicalSwitches {
public:
    enum class Kind : uint8_t { Short, Long };
    template<class Emit> void sample(bool valid, uint8_t pressed, uint32_t now, Emit emit) {
        if (!valid) { armed_ = false; reset(); return; }
        if (!armed_) {
            if (pressed == 0) { armed_ = true; reset(); }
            return;
        }
        for (uint8_t i = 0; i < 8; ++i) {
            Key &key = keys_[i];
            const bool down = (pressed & (1u << i)) != 0;
            if (down != key.raw) { key.raw = down; key.changedAt = now; }
            if (key.stable != down && now - key.changedAt >= 30) {
                key.stable = down;
                if (down) { key.downAt = now; key.longSent = false; }
                else if (!key.longSent) emit(i, Kind::Short);
            }
            if (key.stable && !key.longSent && now - key.downAt >= 1000) {
                key.longSent = true;
                emit(i, Kind::Long);
            }
        }
    }
private:
    struct Key { bool raw = false, stable = false, longSent = false; uint32_t changedAt = 0, downAt = 0; };
    Key keys_[8]{};
    bool armed_ = false;
    void reset() { for (auto &key : keys_) key = {}; }
};
