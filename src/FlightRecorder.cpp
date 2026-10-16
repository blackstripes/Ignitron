#include "FlightRecorder.h"
#include "FlightPending.h"
#include "FlightHeapSample.h"
#include <esp_system.h>
#include <esp_attr.h>
#include <esp_rom_sys.h>
#include <stdarg.h>
#include <stdio.h>

#if defined(PANELAN_SC05X_MODE) && defined(PANELAN_LVGL_UI_MODE) && defined(PANELAN_MINI_SPI_MODE)
namespace {
RTC_NOINIT_ATTR FlightPending::State retained;
uint16_t summary = 0;
uint16_t rawReasons = 0;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

const char *label(FlightRecorder::Status s) {
    switch (s) {
    case FlightRecorder::Valid: return "valid";
    case FlightRecorder::Partial: return "partial";
    case FlightRecorder::Corrupt: return "corrupt";
    default: return "unavailable";
    }
}
const char *resetClass(uint8_t idf, uint8_t raw0, uint8_t raw1) {
    if (raw0 == 21 || raw1 == 21) return "USB-UART chip reset";
    if (raw0 == 22 || raw1 == 22) return "USB-JTAG chip reset";
    switch (static_cast<esp_reset_reason_t>(idf)) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt WDT";
    case ESP_RST_TASK_WDT: return "task WDT";
    case ESP_RST_WDT: return "other WDT";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "SDIO";
    default: return "unknown";
    }
}
// Unlike Print::printf, check the complete byte count: CDC writes can return a
// positive *short* count on disconnect. Formatting remains bounded on stack.
bool send(Stream &out, const char *format, ...) {
    char line[160];
    va_list args;
    va_start(args, format);
    const int n = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    return n > 0 && n < int(sizeof(line)) && out.write(reinterpret_cast<const uint8_t *>(line), n) == size_t(n);
}
bool printRecord(Stream &out, const FlightRing::Record &r) {
    return send(out, " boot=%u seq=%lu ms=%lu event=%u detail=%u value=%u\n", r.boot,
               (unsigned long)r.sequence, (unsigned long)r.ms, r.event, r.detail, r.value);
}
uint16_t goodPending() {
    if (!FlightPending::validMeta(retained)) return 0;
    uint16_t good = 0;
    for (unsigned i = 0; i < retained.pending.header.count; ++i)
        if (FlightRing::matchesSnapshot(retained.pending.header,
                                         FlightRing::at(retained.pending, i), i)) ++good;
    return good;
}
const char *pendingLabel() {
    return label(static_cast<FlightRecorder::Status>(FlightPending::status(retained)));
}
}
namespace FlightRecorder {
void boot() {
    const uint8_t idf = static_cast<uint8_t>(esp_reset_reason());
    const uint8_t raw0 = static_cast<uint8_t>(esp_rom_get_reset_reason(0));
    const uint8_t raw1 = static_cast<uint8_t>(esp_rom_get_reset_reason(1));
    rawReasons = uint16_t(raw0) | (uint16_t(raw1) << 8);
    uint8_t code = 0;
    if (raw0 == 21 || raw1 == 21) code = 11;
    else if (raw0 == 22 || raw1 == 22) code = 12;
    else switch (idf) {
    case ESP_RST_POWERON: code = 1; break;
    case ESP_RST_EXT: code = 2; break;
    case ESP_RST_SW: code = 3; break;
    case ESP_RST_PANIC: code = 4; break;
    case ESP_RST_INT_WDT: code = 5; break;
    case ESP_RST_TASK_WDT: code = 6; break;
    case ESP_RST_WDT: code = 7; break;
    case ESP_RST_DEEPSLEEP: code = 8; break;
    case ESP_RST_BROWNOUT: code = 9; break;
    case ESP_RST_SDIO: code = 10; break;
    default: break;
    }
    summary = (uint16_t(code) << 8) | idf;
    FlightPending::boot(retained, summary, rawReasons);
    // Detail=IDF enum; value packs both raw ROM CPU reasons, including USB 21/22.
    record(Reset, idf, rawReasons);
}
void record(Event event, uint8_t detail, uint16_t value) {
    portENTER_CRITICAL(&mux);
    if (FlightRing::valid(retained.current.header))
        FlightRing::append(retained.current, millis(), event, detail, value);
    portEXIT_CRITICAL(&mux);
}
void heapSample(uint8_t detail) {
    record(HeapSample, detail, FlightHeapSample::pack(ESP.getFreeHeap(), ESP.getMinFreeHeap()));
}
uint16_t resetSummary() { return summary; }
void status(Stream &out) {
    portENTER_CRITICAL(&mux);
    const auto h = retained.current.header;
    portEXIT_CRITICAL(&mux);
    const bool pending = FlightPending::validMeta(retained);
    out.printf("Flight pending=%s good=%u/%u overwritten=%u current=%s boot=%u count=%u/%u overwritten=%u next=%lu RTC=%u bytes\n",
                 pendingLabel(), goodPending(),
                 pending ? retained.pending.header.count : 0,
                 pending ? retained.pending.header.overwritten : 0,
                 label(FlightRing::valid(h) ? Valid : Corrupt), h.boot, h.count,
                 FlightRing::capacity, FlightRing::valid(h) ? h.overwritten : 0,
                 (unsigned long)h.nextSequence, sizeof(retained));
    out.printf("Current reset category=%s IDF=%u rawCPU0=%u rawCPU1=%u\n",
                resetClass(summary & 255, rawReasons & 255, rawReasons >> 8), summary & 255,
                rawReasons & 255, rawReasons >> 8);
    if (pending) out.printf("Pending detected reset category=%s IDF=%u rawCPU0=%u rawCPU1=%u\n",
                            resetClass(retained.meta.summary & 255, retained.meta.rawReasons & 255,
                                       retained.meta.rawReasons >> 8), retained.meta.summary & 255,
                            retained.meta.rawReasons & 255, retained.meta.rawReasons >> 8);
}
void replay(Stream &out) {
    if (&out == &Serial && !Serial) return;
    out.clearWriteError();
    if (!FlightPending::validMeta(retained)) {
        send(out, "[flight] pending=%s; no validated prior trace; currentReset=%s IDF=%u rawCPU0=%u rawCPU1=%u\n",
             pendingLabel(), resetClass(summary & 255, rawReasons & 255, rawReasons >> 8),
             summary & 255, rawReasons & 255, rawReasons >> 8);
        return;
    }
    bool sent = send(out, "[flight] pending=%s valid=%u/%u overwritten=%u detectedReset=%s IDF=%u rawCPU0=%u rawCPU1=%u\n",
                 pendingLabel(), goodPending(), retained.pending.header.count,
                 retained.pending.header.overwritten,
                 resetClass(retained.meta.summary & 255, retained.meta.rawReasons & 255,
                            retained.meta.rawReasons >> 8), retained.meta.summary & 255,
                 retained.meta.rawReasons & 255, retained.meta.rawReasons >> 8);
    for (unsigned i = 0; i < retained.pending.header.count; ++i) {
        const auto r = FlightRing::at(retained.pending, i);
        if (FlightRing::matchesSnapshot(retained.pending.header, r, i))
            sent = send(out, "[flight pending] boot=%u seq=%lu ms=%lu event=%u detail=%u value=%u\n",
                        r.boot, (unsigned long)r.sequence, (unsigned long)r.ms,
                        r.event, r.detail, r.value) && sent;
        else sent = send(out, "[flight pending] slot=%u corrupt\n", i) && sent;
    }
    FlightPending::acknowledgeAfterOutput(retained, sent, out.getWriteError(),
                                           &out != &Serial || static_cast<bool>(Serial));
}
void dump(Stream &out) {
    status(out);
    replay(out);
    portENTER_CRITICAL(&mux);
    const auto h = retained.current.header;
    portEXIT_CRITICAL(&mux);
    if (!FlightRing::valid(h)) return;
    out.printf("[flight] current snapshot count=%u overwritten=%u\n", h.count, h.overwritten);
    for (unsigned i = 0; i < h.count; ++i) {
        portENTER_CRITICAL(&mux);
        // Sequence might advance while dumping: identify overwritten/torn slots.
        const auto r = retained.current.records[(h.write + FlightRing::capacity - h.count + i) % FlightRing::capacity];
        portEXIT_CRITICAL(&mux);
        out.print("[flight current]");
        if (FlightRing::matchesSnapshot(h, r, i)) printRecord(out, r);
        else out.printf(" slot=%u overwritten/torn/corrupt\n", i);
    }
}
}
#else
namespace FlightRecorder {
void boot() {} void record(Event, uint8_t, uint16_t) {}
void heapSample(uint8_t) {}
void status(Stream &out) { out.println("Flight recorder unavailable in this profile"); }
void dump(Stream &out) { status(out); }
void replay(Stream &) {} uint16_t resetSummary() { return 0; }
}
#endif
