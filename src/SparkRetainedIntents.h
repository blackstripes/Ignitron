#pragma once

#include <cstdint>
#include <atomic>

// Named, link-scoped single-shot intentions, not a command or message queue.
// Dispatch builds the command at attempt time; a busy send retains the bit.
class SparkRetainedIntents {
public:
    enum Kind : uint8_t { AmpName, Serial, Checksums, CurrentPreset, LooperConfig, LooperStatus, LooperRecordStatus, Count };
    void request(Kind kind) { pending_.fetch_or(static_cast<uint8_t>(1u << kind)); }
    bool pending(Kind kind) const { return (pending_.load() & (1u << kind)) != 0; }
    void reset() { pending_.store(0); }
    template <typename Dispatch>
    bool service(Dispatch dispatch) {
        for (uint8_t k = 0; k < Count; ++k) {
            Kind kind = static_cast<Kind>(k);
            if (!pending(kind)) continue;
            if (!dispatch(kind)) return false;
            pending_.fetch_and(static_cast<uint8_t>(~(1u << k)));
            return true; // one attempt per service; never race the next query
        }
        return false;
    }
private:
    std::atomic<uint8_t> pending_{0};
};
