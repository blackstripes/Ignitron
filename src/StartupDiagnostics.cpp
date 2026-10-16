#include "StartupDiagnostics.h"
#include <Wire.h>

#if defined(PANELAN_SC05X_MODE) && defined(PANELAN_LVGL_UI_MODE) && defined(PANELAN_MINI_SPI_MODE)
namespace StartupDiagnostics {
RTC_NOINIT_ATTR Breadcrumb breadcrumb;

namespace {
struct Record {
    uint32_t at;
    uint32_t value;
    uint32_t before, after, secondBefore, secondAfter;
    uint32_t clock, timeout;
    uint32_t stack;
    uintptr_t task;
    char taskName[16];
    uint8_t priority, core, status;
    uint8_t event, a, b, c;
};
constexpr uint16_t capacity = 256;
Record ring[capacity];
uint16_t next = 0, count = 0;
uint32_t dropped = 0;

const char *resetReasonName(uint8_t reason) {
    switch (static_cast<esp_reset_reason_t>(reason)) {
    case ESP_RST_POWERON: return "power on";
    case ESP_RST_EXT: return "external reset";
    case ESP_RST_SW: return "software reset";
    case ESP_RST_PANIC: return "panic/exception";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "other watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep wake";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "SDIO";
    default: return "unknown";
    }
}
}

void record(Event event, uint8_t a, uint8_t b, uint8_t c, uint32_t value) {
    if (count == capacity) { ++dropped; return; }
    Record &r = ring[(next + count) % capacity];
    r.at = millis(); r.value = value; r.event = event; r.a = a; r.b = b; r.c = c;
    ++count;
}

void stage(Stage stage, bool complete, bool includeHealth) {
    if (count == capacity) { ++dropped; return; }
    Record &r = ring[(next + count) % capacity];
    r.at = millis(); r.event = StageBoundary; r.a = stage; r.b = complete; r.c = includeHealth;
    if (includeHealth) {
        r.clock = ESP.getFreeHeap();
        r.timeout = ESP.getMinFreeHeap();
        r.stack = uxTaskGetStackHighWaterMark(nullptr);
    }
    ++count;
}

void wireTiming(Event event, uint8_t reg, uint8_t value, uint8_t attempt,
                uint8_t status, uint32_t before, uint32_t after,
                uint32_t secondBefore, uint32_t secondAfter) {
    if (count == capacity) { ++dropped; return; }
    Record &r = ring[(next + count) % capacity];
    r.at = millis(); r.event = event; r.a = reg; r.b = value; r.c = attempt;
    r.status = status; r.before = before; r.after = after;
    r.secondBefore = secondBefore; r.secondAfter = secondAfter;
    ++count;
}

void wireError(uint8_t reg, uint8_t value, uint8_t attempt, uint8_t status) {
    if (count == capacity) { ++dropped; return; }
    Record &r = ring[(next + count) % capacity];
    r.at = millis(); r.event = WireError; r.a = reg; r.b = value;
    r.c = attempt; r.status = status;
    r.clock = Wire.getClock(); r.timeout = Wire.getTimeOut();
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    r.task = reinterpret_cast<uintptr_t>(task);
    const char *name = pcTaskGetName(task);
    size_t i = 0;
    for (; i < sizeof(r.taskName) - 1 && name[i]; ++i) r.taskName[i] = name[i];
    r.taskName[i] = '\0';
    r.priority = uxTaskPriorityGet(task);
    r.core = xPortGetCoreID(); // Running core; this IDF version lacks xTaskGetCoreID.
    ++count;
}

void drain() {
    if (!Serial) return;
    while (count && Serial) {
        const Record r = ring[next];
        next = (next + 1) % capacity;
        --count;
        Serial.printf("[startup %lu ms] ", static_cast<unsigned long>(r.at));
        switch (r.event) {
        case StageBoundary:
            Serial.printf("%s %s", name(r.a), r.b ? "complete" : "enter");
            if (r.c) Serial.printf(" heap=%lu minHeap=%lu stackMin=%lu bytes",
                                   (unsigned long)r.clock, (unsigned long)r.timeout,
                                   (unsigned long)r.stack);
            Serial.println();
            break;
        case ResetReason: Serial.printf("reset reason=%u (%s)\n", r.a, resetReasonName(r.a)); break;
        case PreviousBreadcrumb:
            if (r.c) Serial.printf("previous breadcrumb: %s %s at %lu ms (prior boot uptime)\n",
                                 name(r.a), r.b ? "complete" : "entered", static_cast<unsigned long>(r.value));
            else Serial.println("previous breadcrumb: unavailable/invalid");
            break;
        case McpFault:
            Serial.printf("MCP fault=%u display=%u active=%u\n", r.a, r.b, r.c); break;
        case McpIntent:
            Serial.printf("MCP intent %s IODIRB=0x%02X OLATB=0x%02X display=%u (not readback)\n",
                          r.a == 0 ? "preload before output enable" : r.a == 1 ? "output enable" :
                          r.a == 2 ? "all CS high" : r.a == 3 ? "select CS low" :
                          r.a == 4 ? "reset state" : "final direction/output",
                          r.b, r.c, static_cast<unsigned>(r.value)); break;
        case ResetBoundary:
            Serial.printf("shared reset %s elapsed=%lu ms\n", r.a == 0 ? "low" :
                          r.a == 1 ? "low wait ended" : r.a == 2 ? "high" : "high wait ended",
                          static_cast<unsigned long>(r.value)); break;
        case PanelBoundary:
            Serial.printf("TFT%u %s duration=%lu ms\n", r.a,
                          r.b == InitCall ? "init call" : r.b == InitDone ? "init done" :
                          r.b == SplashBegin ? "splash begin" : r.b == SplashDone ? "splash done" :
                          r.b == LiveBegin ? "first live draw begin" : r.b == LiveDone ? "first live draw done" :
                          r.b == InitRetry ? "init retry" : "draw retry", static_cast<unsigned long>(r.value)); break;
        case MiniMessage:
            Serial.printf("mini message=%u display=%u detail=%lu\n", r.a, r.b, static_cast<unsigned long>(r.value)); break;
        case I2cAbBoundary:
            Serial.printf("I2C/SPI A/B stage=%u boundary=%u success=%u display=%lu\n",
                          r.a, r.b, r.c, static_cast<unsigned long>(r.value)); break;
        case WireRestart:
        case WireBegin:
        case WireRead:
        case WirePause:
        case McpProbe:
        case McpWrite:
        case WireRetry:
            Serial.printf("MCP %s reg=0x%02X value=0x%02X attempt=%u status=%u before=%lu after=%lu elapsed=%lu us",
                          r.event == McpWrite ? "write" : r.event == WireRestart ? "restart" :
                          r.event == WirePause ? "delay(2)" : r.event == WireBegin ? "Wire.begin(12,13)" :
                          r.event == WireRetry ? "retry request" : r.event == WireRead ? "switch read" : "sample",
                          r.a, r.b, r.c, r.status, (unsigned long)r.before,
                          (unsigned long)r.after, (unsigned long)(r.after - r.before));
            if (r.event == McpWrite || r.event == WireRead || (r.event == McpProbe && r.secondBefore))
                Serial.printf(" secondBefore=%lu secondAfter=%lu secondElapsed=%lu us",
                              (unsigned long)r.secondBefore, (unsigned long)r.secondAfter,
                              (unsigned long)(r.secondAfter - r.secondBefore));
            Serial.println();
            break;
        case WireError:
            Serial.printf("MCP error reg=0x%02X value=0x%02X attempt=%u status=%u WireClock=%lu Hz WireTimeout=%lu ms task=%s handle=%p priority=%u core=%u\n",
                          r.a, r.b, r.c, r.status, (unsigned long)r.clock,
                          (unsigned long)r.timeout, r.taskName, reinterpret_cast<void *>(r.task),
                          r.priority, r.core); break;
        }
    }
    if (dropped && Serial) {
        Serial.printf("[startup] ring overflow: %lu events dropped\n", static_cast<unsigned long>(dropped));
        dropped = 0;
    }
}
}
#endif
