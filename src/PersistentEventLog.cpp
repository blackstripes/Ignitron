#include "PersistentEventLog.h"

#include <LittleFS.h>
#include <string.h>

namespace {
constexpr uint32_t kMagic = 0x49474C31UL; // IGL1
constexpr uint32_t kFlushIntervalMs = 5000;
constexpr uint8_t kFlushBatch = 8;
const char *segmentName(uint8_t segment) { return segment == 0 ? "/diag/events.0" : "/diag/events.1"; }
const char *repairName(uint8_t segment) { return segment == 0 ? "/diag/events.0.repair" : "/diag/events.1.repair"; }
portMUX_TYPE eventLogMux = portMUX_INITIALIZER_UNLOCKED;
}

PersistentEventLog persistentEventLog;

uint16_t PersistentEventLog::crc(const Record &record) const {
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&record);
    uint16_t value = 0xFFFF;
    for (size_t i = 0; i < sizeof(Record) - sizeof(record.crc); ++i) {
        value ^= bytes[i];
        for (uint8_t bit = 0; bit < 8; ++bit) value = (value & 1) ? (value >> 1) ^ 0xA001 : value >> 1;
    }
    return value;
}

bool PersistentEventLog::valid(const Record &record) const {
    return record.magic == kMagic && record.sequence != 0 && record.crc == crc(record);
}

bool PersistentEventLog::recoverSegment(uint8_t segment, uint32_t &lastSequence, uint16_t &validRecords) {
    lastSequence = 0;
    validRecords = 0;
    File file = LittleFS.open(segmentName(segment), FILE_READ);
    if (!file) return true; // Missing segment is normal on first boot.
    const size_t fileSize = file.size();
    bool needsRepair = fileSize % sizeof(Record) != 0 || fileSize > kSegmentRecords * sizeof(Record);
    Record record{};
    while (validRecords < kSegmentRecords &&
           file.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) == sizeof(record)) {
        if (!valid(record)) {
            ++corruptRecords_;
            needsRepair = true;
            break;
        }
        lastSequence = record.sequence;
        ++validRecords;
    }
    file.close();
    // Never append after a torn or invalid record.  Rebuild the valid prefix
    // through a temporary file so records before the bad tail remain available.
    return !needsRepair || rewriteSegmentPrefix(segment, validRecords);
}

bool PersistentEventLog::rewriteSegmentPrefix(uint8_t segment, uint16_t validRecords) {
    LittleFS.remove(repairName(segment));
    File source = LittleFS.open(segmentName(segment), FILE_READ);
    File repaired = LittleFS.open(repairName(segment), FILE_WRITE);
    if (!source || !repaired) {
        if (source) source.close();
        if (repaired) repaired.close();
        LittleFS.remove(repairName(segment));
        return false;
    }

    Record record{};
    bool ok = true;
    for (uint16_t i = 0; i < validRecords; ++i) {
        if (source.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) != sizeof(record) ||
            repaired.write(reinterpret_cast<const uint8_t *>(&record), sizeof(record)) != sizeof(record)) {
            ok = false;
            break;
        }
    }
    source.close();
    repaired.close();
    // POSIX/LittleFS rename replaces the destination atomically.  Do not remove
    // the original first: if replacement fails, its valid history remains.
    if (!ok || !LittleFS.rename(repairName(segment), segmentName(segment))) {
        LittleFS.remove(repairName(segment));
        return false;
    }
    return true;
}

void PersistentEventLog::begin(bool filesystemAvailable) {
    filesystemAvailable_ = filesystemAvailable;
    if (!filesystemAvailable_) return;
    LittleFS.mkdir("/diag");
    uint32_t last[2]; uint16_t count[2];
    const bool clean0 = recoverSegment(0, last[0], count[0]);
    const bool clean1 = recoverSegment(1, last[1], count[1]);
    segmentWritable_[0] = clean0;
    segmentWritable_[1] = clean1;
    // If repair failed, retain the file rather than discarding its valid prefix.
    // Do not append to that segment during this boot.
    if (!clean0 && !clean1) filesystemAvailable_ = false;
    activeSegment_ = (clean1 && (!clean0 || last[1] > last[0])) ? 1 : 0;
    activeRecords_ = count[activeSegment_];
    persistedSequence_ = last[activeSegment_];
    const uint32_t highest = last[0] > last[1] ? last[0] : last[1];
    if (highest != 0) {
        nextSequence_ = highest + 1;
        nextFlushSequence_ = nextSequence_;
    }
}

void PersistentEventLog::record(PersistentEvent event, uint16_t value, bool urgent) {
    portENTER_CRITICAL(&eventLogMux);
    if (ringCount_ == kRamRecords) { ++ramOverruns_; } else { ++ringCount_; }
    Record &record = ring_[ringWrite_];
    record.magic = kMagic;
    record.sequence = nextSequence_++;
    record.atMs = millis();
    record.value = value;
    record.event = static_cast<uint8_t>(event);
    record.flags = urgent ? 1 : 0;
    record.crc = crc(record);
    ringWrite_ = (ringWrite_ + 1) % kRamRecords;
    if (urgent) urgentFlush_ = true;
    portEXIT_CRITICAL(&eventLogMux);
}

uint8_t PersistentEventLog::append(const Record *records, uint8_t count) {
    if (activeRecords_ >= kSegmentRecords) {
        const uint8_t nextSegment = activeSegment_ ^ 1;
        // A segment whose repair did not complete may have a non-aligned tail.
        // Preserve it rather than ever appending or rotating through it.
        if (!segmentWritable_[nextSegment]) return 0;
        activeSegment_ = nextSegment;
        LittleFS.remove(segmentName(activeSegment_)); // bounded two-file rotation
        activeRecords_ = 0;
    }
    File file = LittleFS.open(segmentName(activeSegment_), FILE_APPEND);
    if (!file) return 0;
    uint8_t appended = 0;
    // Keep the file open for a batch.  Each completed record is tracked before
    // attempting the next, so a failed write cannot be retried as a duplicate.
    while (appended < count && activeRecords_ < kSegmentRecords) {
        const Record &record = records[appended];
        if (file.write(reinterpret_cast<const uint8_t *>(&record), sizeof(record)) != sizeof(record)) break;
        ++activeRecords_;
        persistedSequence_ = record.sequence;
        ++appended;
    }
    file.close();
    return appended;
}

void PersistentEventLog::service() {
    if (!filesystemAvailable_) return;
    const uint32_t now = millis();
    // Urgent events retain their flag and pending state, but never bypass the
    // boot/write rate limit.
    if (now - lastFlushMs_ < kFlushIntervalMs) return;
    Record batch[kFlushBatch]{};
    uint8_t available = 0;
    portENTER_CRITICAL(&eventLogMux);
    const uint32_t earliest = ringCount_ ? ring_[(ringWrite_ + kRamRecords - ringCount_) % kRamRecords].sequence : nextSequence_;
    if (nextFlushSequence_ < earliest) { ramOverruns_ += earliest - nextFlushSequence_; nextFlushSequence_ = earliest; }
    for (uint16_t i = 0; i < ringCount_ && available < kFlushBatch; ++i) {
        const Record &candidate = ring_[(ringWrite_ + kRamRecords - ringCount_ + i) % kRamRecords];
        if (candidate.sequence == nextFlushSequence_ + available) batch[available++] = candidate;
    }
    portEXIT_CRITICAL(&eventLogMux);

    const uint8_t flushed = append(batch, available);
    if (flushed) {
        portENTER_CRITICAL(&eventLogMux);
        nextFlushSequence_ += flushed;
        if (nextFlushSequence_ >= nextSequence_) urgentFlush_ = false;
        portEXIT_CRITICAL(&eventLogMux);
    }
    // Rate-limit attempted writes too: a persistent filesystem failure must not
    // turn the main loop into a rapid open/write/close retry loop.
    if (available) lastFlushMs_ = now;
}

const char *PersistentEventLog::eventName(uint8_t event) const {
    static const char *const names[] = {"?", "boot", "init", "fs-unavailable", "ble-up", "ble-down", "subscribe-fail", "write-fail", "ingress-busy", "ingress-full", "ingress-reset", "sync-phase", "preset-send", "preset-ok", "preset-fail", "fx-send", "fx-ok", "fx-fail", "loop-send", "loop-ok", "loop-fail", "tuner-send", "tuner-ok", "tuner-fail", "tuner-local-release", "nav", "loop-ack"};
    return event < sizeof(names) / sizeof(names[0]) ? names[event] : "unknown";
}
void PersistentEventLog::printRecord(Stream &out, const Record &record) const {
    out.printf("%lu %lu %s %u%s\n", static_cast<unsigned long>(record.sequence), static_cast<unsigned long>(record.atMs), eventName(record.event), record.value, record.flags ? " urgent" : "");
}
void PersistentEventLog::printStatus(Stream &out) const {
    out.printf("Event log: %s, active events.%u (%u/%u), persisted=%lu, RAM=%u, overruns=%lu, corrupt=%lu\n",
               filesystemAvailable_ ? "flash" : "RAM-only", activeSegment_, activeRecords_, kSegmentRecords,
               static_cast<unsigned long>(persistedSequence_), ringCount_, static_cast<unsigned long>(ramOverruns_), static_cast<unsigned long>(corruptRecords_));
}
void PersistentEventLog::dump(Stream &out) {
    if (filesystemAvailable_) for (uint8_t segment = 0; segment < 2; ++segment) {
        File file = LittleFS.open(segmentName(segment), FILE_READ); Record record;
        while (file && file.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) == sizeof(record)) if (valid(record)) printRecord(out, record); else break;
        if (file) file.close();
    }
    Record ramSnapshot[kRamRecords]{};
    uint16_t snapshotCount = 0;
    portENTER_CRITICAL(&eventLogMux);
    const uint32_t firstUnflushed = nextFlushSequence_;
    for (uint16_t i = 0; i < ringCount_; ++i) {
        const Record &record = ring_[(ringWrite_ + kRamRecords - ringCount_ + i) % kRamRecords];
        if (record.sequence >= firstUnflushed) ramSnapshot[snapshotCount++] = record;
    }
    portEXIT_CRITICAL(&eventLogMux);
    for (uint16_t i = 0; i < snapshotCount; ++i) printRecord(out, ramSnapshot[i]);
}
bool PersistentEventLog::clear() {
    if (!filesystemAvailable_) return false;
    LittleFS.remove(segmentName(0)); LittleFS.remove(segmentName(1));
    activeSegment_ = 0; activeRecords_ = 0; persistedSequence_ = 0;
    portENTER_CRITICAL(&eventLogMux); ringCount_ = 0; ringWrite_ = 0; nextFlushSequence_ = nextSequence_; portEXIT_CRITICAL(&eventLogMux);
    return true;
}
