#pragma once

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
    const char *description = "unknown";
    switch (reason) {
    case ESP_RST_POWERON: description = "power on"; break;
    case ESP_RST_EXT: description = "external reset"; break;
    case ESP_RST_SW: description = "software reset"; break;
    case ESP_RST_PANIC: description = "panic/exception"; break;
    case ESP_RST_INT_WDT: description = "interrupt watchdog"; break;
    case ESP_RST_TASK_WDT: description = "task watchdog"; break;
    case ESP_RST_WDT: description = "other watchdog"; break;
    case ESP_RST_DEEPSLEEP: description = "deep sleep wake"; break;
    case ESP_RST_BROWNOUT: description = "brownout"; break;
    case ESP_RST_SDIO: description = "SDIO"; break;
    default: break;
    }
    Serial.printf("[startup %lu ms] reset=%d (%s)", static_cast<unsigned long>(millis()),
                  static_cast<int>(reason), description);
    health();
    if (breadcrumb.marker == 0x49474E44u && breadcrumb.stage <= InitComplete &&
        breadcrumb.complete <= 1 && breadcrumb.reserved == 0 && breadcrumb.checksum == check(breadcrumb)) {
        Serial.printf("[startup] previous breadcrumb: %s %s at %lu ms (prior boot uptime)\n",
                      name(breadcrumb.stage), breadcrumb.complete ? "complete" : "entered",
                      static_cast<unsigned long>(breadcrumb.timestamp));
    } else {
        Serial.println("[startup] previous breadcrumb: unavailable/invalid");
    }
}

inline void mark(Stage stage, bool complete, bool log = true) {
    breadcrumb.marker = 0x49474E44u;
    breadcrumb.timestamp = millis();
    breadcrumb.stage = stage;
    breadcrumb.complete = complete;
    breadcrumb.reserved = 0;
    breadcrumb.checksum = check(breadcrumb);
    if (log) {
        Serial.printf("[startup %lu ms] %s %s", static_cast<unsigned long>(breadcrumb.timestamp),
                      name(stage), complete ? "complete" : "enter");
        health();
    }
}

inline Stage tftInit(uint8_t display) { return static_cast<Stage>(Tft1Init + (display - 1) * 2); }
inline Stage tftSplash(uint8_t display) { return static_cast<Stage>(Tft1Splash + (display - 1) * 2); }
} // namespace StartupDiagnostics
#endif
