#pragma once

#include <cstdint>

// Controller-task-owned, bounded and payload-/identity-free. Timestamps are
// millis() values; flags distinguish a real timestamp of zero from absence.
class SparkTransportTelemetry {
public:
    enum class Reason : uint8_t { Pending, Sent, Response, Timeout, Invalidated, Revoked, LinkReset, WriteFailed, Confirmed, Failed };
    struct Record {
        uint32_t id = 0, accepted = 0, sendStart = 0, sendEnd = 0;
        uint32_t firstNotification = 0, parsed = 0, semantic = 0;
        uint16_t chunks = 0, retries = 0, invalidations = 0;
        uint8_t kind = 0; // 1=query, 2=mutation
        Reason reason = Reason::Pending;
        bool started = false, written = false, notified = false, completed = false, confirmed = false;
    };
    struct Bucket { uint32_t count[5] = {}; void add(uint32_t ms) {
        ++count[ms < 100 ? 0 : ms < 500 ? 1 : ms < 2000 ? 2 : ms < 5000 ? 3 : 4];
    } };

    uint32_t begin(uint8_t kind, uint32_t now) {
        Record &r = records_[nextId_ % capacity];
        if (r.id && r.id == activeQueryId_) activeQuery_ = r;
        r = {};
        r.id = ++nextId_;
        r.kind = kind;
        r.accepted = now;
        if (kind == 1 && !activeQueryId_) activeQueryId_ = r.id;
        return r.id;
    }
    Record *get(uint32_t id) {
        if (!id) return nullptr;
        Record &r = records_[(id - 1) % capacity];
        return r.id == id ? &r : nullptr;
    }
    const Record *get(uint32_t id) const { return const_cast<SparkTransportTelemetry *>(this)->get(id); }
    void start(uint32_t id, uint32_t now) {
        if (auto *r = lifecycle(id)) if (!r->started) {
            r->started = true; r->sendStart = now; queueDelay.add(now - r->accepted);
        }
    }
    // Each part contributes chunks and end time; only the final successful
    // part marks the logical transaction sent.
    void write(uint32_t id, uint32_t now, uint16_t chunks, bool success, bool finalPart = true) {
        if (auto *r = lifecycle(id)) {
            r->sendEnd = now; r->chunks += chunks;
            if (!success) { r->reason = Reason::WriteFailed; ++writeFailed; releaseQuery(id); }
            else if (finalPart) { if (!r->written) ++sent; r->written = true; r->reason = Reason::Sent; }
        }
    }
    void notification(uint32_t id, uint32_t now) {
        if (auto *r = lifecycle(id)) if (r->written && !r->notified) {
            r->notified = true; r->firstNotification = now;
            // A synchronous notification can arrive before writeBLE returns.
            if (static_cast<int32_t>(now - r->sendEnd) >= 0) firstResponse.add(now - r->sendEnd);
        }
    }
    void response(uint32_t id, uint32_t now) {
        if (auto *r = lifecycle(id)) if (r->written && !r->completed) {
            r->completed = true; r->parsed = now; r->reason = Reason::Response;
            completeResponse.add(now - r->sendEnd); ++responses;
            releaseQuery(id);
        }
    }
    // Action confirmation can outlive the 16 most recent transport writes.
    // Pin only the write-completion timestamp, not an extra payload record.
    void pinSemantic(uint32_t id, unsigned slot) {
        if (slot >= 2) return;
        const Record *r = get(id);
        semanticPins_[slot] = r && r->written ? SemanticPin{id, r->sendEnd} : SemanticPin{};
    }
    void semantic(uint32_t id, uint32_t now) {
        Record *r = get(id);
        SemanticPin *pin = nullptr;
        for (auto &candidate : semanticPins_) if (candidate.id == id && id) pin = &candidate;
        if (r && r->written && !r->confirmed && r->reason != Reason::Failed &&
            r->reason != Reason::LinkReset) {
            r->confirmed = true; r->semantic = now; r->reason = Reason::Confirmed;
            confirmation.add(now - r->sendEnd); ++confirms;
        } else if (!r && pin) {
            confirmation.add(now - pin->sendEnd); ++confirms;
        }
        if (pin) *pin = {};
    }
    void retry(uint32_t id) { if (auto *r = lifecycle(id)) { ++r->retries; ++retries; } }
    // An ingress loss resets the parser even when no query owns the lane.
    // Count the reset once, and associate it with the owner only if present.
    void ingressInvalidation(uint32_t id) {
        ++invalidations;
        end(id, Reason::Invalidated);
    }
    void end(uint32_t id, Reason reason) {
        if (reason == Reason::Failed || reason == Reason::LinkReset)
            for (auto &pin : semanticPins_) if (pin.id == id) pin = {};
        if (auto *r = lifecycle(id)) {
            if (r->reason == Reason::Response || r->reason == Reason::Timeout ||
                r->reason == Reason::Invalidated || r->reason == Reason::Revoked ||
                r->reason == Reason::LinkReset) return;
            r->reason = reason;
            if (reason == Reason::Timeout) ++timeouts;
            if (reason == Reason::Invalidated) ++r->invalidations;
        }
        releaseQuery(id);
    }
    static constexpr unsigned capacity = 16;
    uint32_t lastId() const { return nextId_; }
    const Record &recent(unsigned index) const { return records_[(nextId_ - 1 - index) % capacity]; }
    Bucket queueDelay, firstResponse, completeResponse, confirmation;
    uint32_t sent = 0, writeFailed = 0, responses = 0, confirms = 0, retries = 0, timeouts = 0, invalidations = 0;
private:
    Record *lifecycle(uint32_t id) {
        if (auto *r = get(id)) return r;
        return id && id == activeQueryId_ && activeQuery_.id == id ? &activeQuery_ : nullptr;
    }
    void releaseQuery(uint32_t id) {
        if (id && id == activeQueryId_) { activeQueryId_ = 0; activeQuery_ = {}; }
    }
    struct SemanticPin { uint32_t id = 0, sendEnd = 0; };
    SemanticPin semanticPins_[2] = {};
    // One response lane can own a query at a time. Keep its lifecycle after
    // ring eviction without extending the bounded recent-record diagnostics.
    Record activeQuery_ = {};
    uint32_t activeQueryId_ = 0;
    Record records_[capacity] = {};
    uint32_t nextId_ = 0;
};
