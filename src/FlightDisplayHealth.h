#pragma once
#include <stdint.h>
#include <atomic>

// Counters are loop-owned. Only Progress is shared with the passive core-0 observer.
namespace FlightDisplayHealth {
inline uint16_t clamp(uint32_t n) { return n > 65535 ? 65535 : uint16_t(n); }
inline uint8_t byte(uint32_t n) { return n > 255 ? 255 : uint8_t(n); }
inline uint16_t pair(uint32_t low, uint32_t high) { return uint16_t(byte(low) | (uint16_t(byte(high)) << 8)); }
inline bool slowFlush(uint32_t durationMs) { return durationMs >= 50; }
struct Counters {
    uint32_t iterations = 0, lastLoop = 0, maxGap = 0;
    uint32_t services = 0, serviceBegins = 0, serviceBegin = 0, serviceEnd = 0, maxServiceMs = 0;
    uint32_t requests = 0, invalidations = 0, revisions = 0, ready = 0, flushes = 0;
    uint32_t lastFlush = 0, maxFlushMs = 0, lastFlushMs = 0;
    uint32_t requestedAt = 0;
    uint32_t slowestFlush = 0;
    bool pending = false, reported = false;
    void loop(uint32_t now) {
        if (iterations) { const uint32_t gap = now - lastLoop; if (gap > maxGap) maxGap = gap; }
        lastLoop = now;
        ++iterations;
    }
    void request(uint32_t now) { ++requests; if (!pending) { pending = true; requestedAt = now; reported = false; } }
    void invalidationCallback() { ++invalidations; }
    void flushed(uint32_t now, uint32_t ms) {
        ++flushes; lastFlush = now; lastFlushMs = ms;
        if (ms > maxFlushMs) maxFlushMs = ms;
        if (slowFlush(ms) && ms > slowestFlush) slowestFlush = ms;
        // A partial refresh can have more flushes; only refresh-ready closes it.
    }
    void refreshReady() { ++ready; pending = false; reported = false; }
    bool pendingAlarm(uint32_t now) {
        if (reported || !pending || uint32_t(now - requestedAt) < 2000) return false;
        reported = true; return true;
    }
};
enum Stall : uint8_t { None, ControllerLoopStall, LvglServiceStall, DisplayFlushStall };
// Release/acquire publishes the start timestamp before the in-progress flag.
// A brief stale classification at a boundary is possible; no display work is changed.
struct Progress {
    std::atomic<uint32_t> loopAt{0}, serviceAt{0}, flushAt{0}, observerStackBytes{0};
    std::atomic<bool> servicing{false}, flushing{false};
    std::atomic<uint8_t> diagnosticDepth{0};
    // Valid elapsed intervals are less than half the uint32 millis range.
    // A publication just after the sampled now is a future timestamp, age zero.
    static uint32_t elapsed(uint32_t now, uint32_t at) {
        const uint32_t delta = now - at;
        return delta < 0x80000000u ? delta : 0;
    }
    void loop(uint32_t now) { loopAt.store(now, std::memory_order_release); }
    void serviceStart(uint32_t now) { serviceAt.store(now, std::memory_order_relaxed); servicing.store(true, std::memory_order_release); }
    void serviceDone() { servicing.store(false, std::memory_order_release); }
    void flushStart(uint32_t now) { flushAt.store(now, std::memory_order_relaxed); flushing.store(true, std::memory_order_release); }
    void flushDone() { flushing.store(false, std::memory_order_release); }
    Stall classify(uint32_t now, uint32_t threshold = 2000) const {
        if (flushing.load(std::memory_order_acquire) && elapsed(now, flushAt.load(std::memory_order_relaxed)) >= threshold) return DisplayFlushStall;
        if (servicing.load(std::memory_order_acquire) && elapsed(now, serviceAt.load(std::memory_order_relaxed)) >= threshold) return LvglServiceStall;
        if (diagnosticDepth.load(std::memory_order_acquire) == 0 &&
            elapsed(now, loopAt.load(std::memory_order_acquire)) >= threshold) return ControllerLoopStall;
        return None;
    }
    uint32_t age(Stall s, uint32_t now) const {
        return elapsed(now, s == DisplayFlushStall ? flushAt.load() :
                            s == LvglServiceStall ? serviceAt.load() : loopAt.load());
    }
};
// Scoped exemption for known-long synchronous serial output, not display work.
struct ScopedDiagnosticOutput {
    Progress &progress;
    uint32_t (*clock)();
    ScopedDiagnosticOutput(Progress &p, uint32_t (*c)()) : progress(p), clock(c) {
        progress.loop(clock());
        progress.diagnosticDepth.fetch_add(1, std::memory_order_acq_rel);
    }
    ~ScopedDiagnosticOutput() {
        progress.loop(clock());
        progress.diagnosticDepth.fetch_sub(1, std::memory_order_acq_rel);
    }
    ScopedDiagnosticOutput(const ScopedDiagnosticOutput &) = delete;
    ScopedDiagnosticOutput &operator=(const ScopedDiagnosticOutput &) = delete;
};
struct Observer {
    Stall reported = None;
    template <typename Emit> void poll(const Progress &p, uint32_t now, Emit emit) {
        const Stall state = p.classify(now);
        if (state == reported) return;
        if (reported != None) emit(None, reported, 0); // recovery / reclassification
        if (state != None) emit(state, None, clamp(p.age(state, now)));
        reported = state;
    }
};
extern Counters health;
extern Progress progress;
}
