#pragma once
#include <Arduino.h>
#include "FlightRing.h"

namespace FlightRecorder {
enum Event : uint8_t {
    Reset = 1, Stage, RestartIntent, Scan, Connect, Link, Sync, Preset, Fx,
    Parser, Invalidate, Mcp, TaskFailure, AllocationFailure, StateFault, HeapSample,
    DisplayHealth, DisplayProgress, DisplayTiming, DisplayState, DisplaySlow, DisplayPending, DisplayStall
};
enum Status : uint8_t { Unavailable, Valid, Partial, Corrupt };
// Call before serial wait and before other producers start.
void boot();
// RAM/RTC only; callback safe. No Serial or filesystem I/O.
void record(Event event, uint8_t detail = 0, uint16_t value = 0);
// Snapshot ESP.getFreeHeap()/getMinFreeHeap() into one retained record; no I/O.
void heapSample(uint8_t detail);
void status(Stream &out);
void dump(Stream &out);
void replay(Stream &out); // read-only pending boot
bool acknowledgePending(uint16_t expectedBootId);
uint16_t resetSummary(); // high byte classification, low byte IDF enum
}
