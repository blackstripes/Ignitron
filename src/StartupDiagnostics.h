#pragma once

namespace StartupDiagnostics {
enum Event : unsigned char {
    StageBoundary, ResetReason, PreviousBreadcrumb, McpWrite, McpFault,
    McpIntent, ResetBoundary, PanelBoundary, MiniMessage,
    WireRestart, WirePause, McpProbe, WireError, WireRetry, WireBegin, WireRead,
    I2cAbBoundary
};
enum Fault : unsigned char { NotReady, InvalidSelect, Overlap, InvalidDeselect, SelectFailed, DeselectFailed, SetupFailed };
enum PanelPhase : unsigned char { InitCall, InitDone, SplashBegin, SplashDone, LiveBegin, LiveDone, InitRetry, DrawRetry };
enum Message : unsigned char { SpiBegin, McpReady, McpDisabled, PanelReady, CardSent };
// A/B boundaries: stage 3 uses 0=start, 1=pre-SPI CS-high write,
// 2=SPI bus init, 3=post-init CS-high write, 4=empty SPI transaction and
// post-check, 5=byte transaction, 6=post-transfer CS-high write, 7=bus release,
// 8=post-release CS-high.
// Stage 2 uses 0=start, 1=post-init deselect, 2=reselect result.
// Stage 4 uses boundaries 0-15 around raw IDF bus init, add-device, transfer,
// and release, with MCP high-write checks between each phase where possible.
}

#if defined(PANELAN_SC05X_MODE) && defined(PANELAN_LVGL_UI_MODE) && defined(PANELAN_MINI_SPI_MODE)
#include <Arduino.h>
#include <esp_system.h>
#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace StartupDiagnostics {
enum Stage : uint8_t {
    Boot, MainDisplay, SparkInit, McpBegin,
    Tft1Init, Tft1Splash, Tft2Init, Tft2Splash, Tft3Init, Tft3Splash,
    Tft4Init, Tft4Splash, Tft5Init, Tft5Splash, Tft6Init, Tft6Splash,
    MainUiUpdate, MiniUiUpdate, InitComplete
};

struct Breadcrumb {
    uint32_t marker;
    uint32_t timestamp;
    uint8_t stage;
    uint8_t complete;
    uint16_t reserved;
    uint32_t checksum;
};
extern Breadcrumb breadcrumb;

// Single controller-task producer/consumer. No heap or Serial work on the
// producer path, including while a mini CS is asserted.
void record(Event event, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0,
            uint32_t value = 0);
void stage(Stage stage, bool complete, bool includeHealth);
// Microsecond call boundaries are captured by the caller, adjacent to Wire calls.
void wireTiming(Event event, uint8_t reg, uint8_t value, uint8_t attempt,
                uint8_t status, uint32_t before, uint32_t after,
                uint32_t secondBefore = 0, uint32_t secondAfter = 0);
void wireError(uint8_t reg, uint8_t value, uint8_t attempt, uint8_t status);
void drain(); // Only from setup/loop, outside SPI work; requires connected USB CDC.

inline const char *name(uint8_t stage) {
    static const char *const names[] = {
        "boot", "main display begin", "Spark/BLE init", "mini MCP begin",
        "TFT1 init", "TFT1 splash", "TFT2 init", "TFT2 splash",
        "TFT3 init", "TFT3 splash", "TFT4 init", "TFT4 splash",
        "TFT5 init", "TFT5 splash", "TFT6 init", "TFT6 splash",
        "main UI update", "mini UI transition/update", "init complete"
    };
    return stage <= InitComplete ? names[stage] : "invalid";
}

inline uint32_t check(const Breadcrumb &b) {
    return b.marker ^ b.timestamp ^ (uint32_t(b.stage) << 16) ^
           (uint32_t(b.complete) << 8) ^ b.reserved ^ 0x91A2B3C4u;
}

inline void health() {
    // IDF's ESP32 port reports the task stack high-water mark in bytes.
    Serial.printf(" heap=%lu minHeap=%lu stackMin=%lu bytes\n",
                  static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(ESP.getMinFreeHeap()),
                  static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr)));
}

inline void boot() {
    const esp_reset_reason_t reason = esp_reset_reason();
    record(ResetReason, static_cast<uint8_t>(reason));
    if (breadcrumb.marker == 0x49474E44u && breadcrumb.stage <= InitComplete &&
        breadcrumb.complete <= 1 && breadcrumb.reserved == 0 && breadcrumb.checksum == check(breadcrumb)) {
        record(PreviousBreadcrumb, breadcrumb.stage, breadcrumb.complete, 1, breadcrumb.timestamp);
    } else {
        record(PreviousBreadcrumb);
    }
}

inline void mark(Stage stage, bool complete, bool log = true) {
    breadcrumb.marker = 0x49474E44u;
    breadcrumb.timestamp = millis();
    breadcrumb.stage = stage;
    breadcrumb.complete = complete;
    breadcrumb.reserved = 0;
    breadcrumb.checksum = check(breadcrumb);
    StartupDiagnostics::stage(stage, complete, log);
}

inline Stage tftInit(uint8_t display) { return static_cast<Stage>(Tft1Init + (display - 1) * 2); }
inline Stage tftSplash(uint8_t display) { return static_cast<Stage>(Tft1Splash + (display - 1) * 2); }
} // namespace StartupDiagnostics
#else
#include <stdint.h>
namespace StartupDiagnostics {
inline void record(Event, uint8_t = 0, uint8_t = 0, uint8_t = 0, uint32_t = 0) {}
inline void stage(Stage, bool, bool) {}
inline void wireTiming(Event, uint8_t, uint8_t, uint8_t, uint8_t,
                       uint32_t, uint32_t, uint32_t = 0, uint32_t = 0) {}
inline void wireError(uint8_t, uint8_t, uint8_t, uint8_t) {}
}
#endif
