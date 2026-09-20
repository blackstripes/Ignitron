#ifndef PERSISTENT_EVENT_LOG_H_
#define PERSISTENT_EVENT_LOG_H_

#include <Arduino.h>

// Compact diagnostic codes only.  Never put protocol payloads, names, or other
// user/device strings in this log.
enum class PersistentEvent : uint8_t {
    Boot = 1, InitComplete, FilesystemUnavailable,
    BleLinkUp, BleLinkDown, BleSubscribeFailure, BleWriteFailure,
    IngressDropBusy, IngressDropFull, IngressInvalidated,
    SyncPhase,
    PresetSend, PresetConfirmed, PresetFailed,
    FxSend, FxConfirmed, FxFailed
};

class PersistentEventLog {
public:
    void begin(bool filesystemAvailable);
    // Safe to call from BLE callbacks: RAM bookkeeping only, no filesystem IO.
    void record(PersistentEvent event, uint16_t value = 0, bool urgent = false);
    // Must be called from Arduino's main loop; it is the only normal flush path.
    void service();
    void printStatus(Stream &out) const;
    void dump(Stream &out);
    bool clear(); // Call from the main loop / serial command context only.

private:
    static constexpr uint8_t kRamRecords = 64;
    static constexpr uint16_t kSegmentRecords = 128;
    struct __attribute__((packed)) Record {
        uint32_t magic;
        uint32_t sequence;
        uint32_t atMs;
        uint16_t value;
        uint8_t event;
        uint8_t flags;
        uint16_t crc;
    };

    Record ring_[kRamRecords]{};
    uint32_t nextSequence_ = 1;
    uint32_t nextFlushSequence_ = 1;
    uint32_t persistedSequence_ = 0;
    uint16_t ringCount_ = 0;
    uint16_t ringWrite_ = 0;
    uint16_t activeRecords_ = 0;
    uint8_t activeSegment_ = 0;
    bool segmentWritable_[2] = {true, true};
    bool filesystemAvailable_ = false;
    bool urgentFlush_ = false;
    uint32_t lastFlushMs_ = 0;
    uint32_t ramOverruns_ = 0;
    uint32_t corruptRecords_ = 0;

    uint16_t crc(const Record &record) const;
    bool valid(const Record &record) const;
    bool recoverSegment(uint8_t segment, uint32_t &lastSequence, uint16_t &validRecords);
    bool rewriteSegmentPrefix(uint8_t segment, uint16_t validRecords);
    uint8_t append(const Record *records, uint8_t count);
    void printRecord(Stream &out, const Record &record) const;
    const char *eventName(uint8_t event) const;
};

extern PersistentEventLog persistentEventLog;

#endif
