#pragma once
#include <cstdint>

// Browsing never changes Spark-owned confirmed or pending preset state.
class PhysicalPresetBank {
public:
    uint8_t bank() const { return bank_; } // zero based
    void clamp(uint8_t count) { if (!count || bank_ >= (count + 3) / 4) bank_ = 0; }
    // Only a new fresh confirmation (including the first after a stale link)
    // relocates browsing. Repeated snapshots must not undo local bank steps.
    void observe(uint8_t confirmed, bool fresh, uint8_t count) {
        clamp(count);
        if (!fresh || !confirmed || confirmed > count) {
            lastConfirmed_ = 0;
            return;
        }
        if (confirmed != lastConfirmed_) bank_ = static_cast<uint8_t>((confirmed - 1) / 4);
        lastConfirmed_ = confirmed;
    }
    void step(int direction, uint8_t count) {
        clamp(count);
        if (direction < 0 && bank_) --bank_;
        if (direction > 0 && bank_ + 1 < (count + 3) / 4) ++bank_;
    }
    uint8_t target(uint8_t key, uint8_t count) const {
        const unsigned slot = bank_ * 4u + key + 1u;
        return key < 4 && slot <= count ? static_cast<uint8_t>(slot) : 0;
    }
private:
    uint8_t bank_ = 0;
    uint8_t lastConfirmed_ = 0;
};
