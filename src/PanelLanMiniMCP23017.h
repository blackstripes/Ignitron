#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "StartupDiagnostics.h"

// MCP23017 at 0x27 (Waveshare board address pins open/high), default BANK=0.
// Port A: eight active-low switches with internal pull-ups.
// GPB0..5 = active-low TFT1..6 CS; GPB6 = shared active-low reset.
// GPB7 remains an input. No dependency on the main PanelLan touch I2C bus.
class PanelLanMiniMCP23017 {
    static constexpr uint8_t address_ = 0x27;
    static constexpr uint8_t csMask_ = 0x3f;
    static constexpr uint8_t resetMask_ = 0x40;
    static constexpr uint8_t high_ = csMask_ | resetMask_;
    static constexpr uint8_t olatB_ = 0x15;
    static constexpr uint8_t iodirB_ = 0x01;
    static constexpr uint8_t iodirA_ = 0x00;
    static constexpr uint8_t gppuA_ = 0x0c;
    static constexpr uint8_t gpioA_ = 0x12;
    bool ready_ = false;
    uint8_t active_ = 0; // Software intent only; no MCP electrical readback.
    bool probeDone_ = false;
    bool probePending_ = false;

    void restart(uint8_t reg, uint8_t value, uint8_t attempt) {
        const uint32_t endAt = micros();
        const bool ended = Wire.end();
        const uint32_t endDone = micros();
        StartupDiagnostics::wireTiming(StartupDiagnostics::WireRestart, reg, value, attempt,
                                       ended, endAt, endDone);
        const uint32_t delayAt = micros();
        delay(2); // Existing retry wait, unchanged.
        const uint32_t delayDone = micros();
        StartupDiagnostics::wireTiming(StartupDiagnostics::WirePause, reg, value, attempt,
                                       0, delayAt, delayDone);
        const uint32_t beginAt = micros();
        const bool begun = Wire.begin(12, 13);
        const uint32_t beginDone = micros();
        StartupDiagnostics::wireTiming(StartupDiagnostics::WireBegin, reg, value, attempt,
                                       begun, beginAt, beginDone);
    }

    // BANK=0 register pointer write with STOP: never leave TwoWire's no-STOP
    // transaction mutex held if the subsequent request fails.
    void probe(uint8_t reg) {
        const uint32_t beginAt = micros();
        Wire.beginTransmission(address_);
        const uint32_t beginDone = micros();
        Wire.write(reg);
        const uint32_t endAt = micros();
        const uint8_t status = Wire.endTransmission(true);
        const uint32_t endDone = micros();
        StartupDiagnostics::wireTiming(StartupDiagnostics::McpWrite, reg, 0xff, 0,
                                       status, beginAt, beginDone, endAt, endDone);
        if (status != 0) {
            StartupDiagnostics::wireError(reg, 0xff, 0, status);
            StartupDiagnostics::wireTiming(StartupDiagnostics::McpProbe, reg, 0xff, 0,
                                           status, beginAt, endDone);
            return;
        }
        const uint32_t requestAt = micros();
        const uint8_t count = Wire.requestFrom(address_, uint8_t(1), uint8_t(1));
        const uint32_t requestDone = micros();
        const bool valid = count == 1 && Wire.available();
        const uint8_t sample = valid ? Wire.read() : 0xff;
        StartupDiagnostics::wireTiming(StartupDiagnostics::McpProbe, reg, sample, count,
                                       status, beginAt, endDone, requestAt, requestDone);
        if (!valid) StartupDiagnostics::wireError(reg, sample, 0, count ? 0xfe : 0xfd);
    }

    void probeAfterRecoveredHigh() {
        if (!probePending_ || probeDone_) return;
        probeDone_ = true;
        probePending_ = false;
        probe(iodirB_);
        probe(0x13); // GPIOB, sampled pin state (not the requested OLATB latch).
        probe(olatB_);
    }

    bool write(uint8_t reg, uint8_t value) {
        for (uint8_t attempt = 0; attempt < 2; ++attempt) {
            if (attempt) {
                const uint32_t retryAt = micros();
                StartupDiagnostics::wireTiming(StartupDiagnostics::WireRetry, reg, value,
                                               attempt + 1, 0, retryAt, retryAt);
            }
            const uint32_t beginAt = micros();
            Wire.beginTransmission(address_);
            const uint32_t beginDone = micros();
            Wire.write(reg);
            Wire.write(value);
            const uint32_t endAt = micros();
            const uint8_t status = Wire.endTransmission();
            const uint32_t endDone = micros();
            StartupDiagnostics::wireTiming(StartupDiagnostics::McpWrite, reg, value, attempt + 1,
                                           status, beginAt, beginDone, endAt, endDone);
            if (status == 0) {
                // Only probe after GPB directions have been configured. A
                // recovered preload while the pins are still inputs would not
                // tell us whether the CS/RESET outputs subsequently latched.
                if (attempt && ready_ && reg == olatB_ && value == high_ && !probeDone_)
                    probePending_ = true;
                return true;
            }
            StartupDiagnostics::wireError(reg, value, attempt + 1, status);
            if (attempt == 1) {
#if !defined(PANELAN_MINI_SPI_MODE)
                Serial.printf("MCP23017: write reg 0x%02X failed (I2C status %u)\n", reg, status);
#endif
                return false;
            }

            // SPI/display initialization can leave the shared Wire peripheral
            // needing a restart. Rebind only after a failed transaction, then
            // retry once; successful transactions keep the bus untouched.
            restart(reg, value, attempt + 1);
        }
        return false;
    }

public:
    bool begin() {
#if defined(PANELAN_SC05X_MODE) && defined(PANELAN_LVGL_UI_MODE) && defined(PANELAN_MINI_SPI_MODE)
        StartupDiagnostics::mark(StartupDiagnostics::McpBegin, false);
#endif
        ready_ = false;
        active_ = 0;
        probePending_ = false;
        const uint32_t beginAt = micros();
        const bool begun = Wire.begin(12, 13); // IO12 SDA, IO13 SCL (not TFT RES/CS)
        const uint32_t beginDone = micros();
        StartupDiagnostics::wireTiming(StartupDiagnostics::WireBegin, 0, 0, 0,
                                       begun, beginAt, beginDone);
        // Preload the latch *while pins are still inputs*: no selected TFT
        // when switching GPB0..6 to outputs. GPB7 is left as an input.
        StartupDiagnostics::record(StartupDiagnostics::McpIntent, 0, 0xff, high_);
        if (!write(olatB_, high_)) {
            StartupDiagnostics::record(StartupDiagnostics::McpFault, StartupDiagnostics::SetupFailed);
#if !defined(PANELAN_MINI_SPI_MODE)
            Serial.println("MCP23017: no ACK at 0x27 or port-B setup failed");
#endif
            return false;
        }
        probeAfterRecoveredHigh();
        StartupDiagnostics::record(StartupDiagnostics::McpIntent, 1, 0x80, high_);
        if (!write(iodirB_, 0x80) || !write(iodirA_, 0xff) || !write(gppuA_, 0xff)) {
            StartupDiagnostics::record(StartupDiagnostics::McpFault, StartupDiagnostics::SetupFailed);
#if !defined(PANELAN_MINI_SPI_MODE)
            Serial.println("MCP23017: no ACK at 0x27 or port-B setup failed");
#endif
            return false;
        }
        ready_ = true;
        delay(10);
        StartupDiagnostics::record(StartupDiagnostics::McpIntent, 4, 0x80, csMask_);
        if (!write(olatB_, csMask_)) { ready_ = false; return false; }
        const uint32_t lowAt = millis();
        StartupDiagnostics::record(StartupDiagnostics::ResetBoundary, 0);
        delay(20);
        StartupDiagnostics::record(StartupDiagnostics::ResetBoundary, 1, 0, 0, millis() - lowAt);
        StartupDiagnostics::record(StartupDiagnostics::McpIntent, 4, 0x80, high_);
        if (!write(olatB_, high_)) { ready_ = false; return false; }
        probeAfterRecoveredHigh();
        const uint32_t highAt = millis();
        StartupDiagnostics::record(StartupDiagnostics::ResetBoundary, 2);
        delay(120);
        StartupDiagnostics::record(StartupDiagnostics::ResetBoundary, 3, 0, 0, millis() - highAt);
        StartupDiagnostics::record(StartupDiagnostics::McpIntent, 5, 0x80, high_);
        StartupDiagnostics::record(StartupDiagnostics::MiniMessage, StartupDiagnostics::McpReady);
#if !defined(PANELAN_MINI_SPI_MODE)
        Serial.println("MCP23017: detected at 0x27; GPB0-GPB6 ready, shared TFT1..TFT6 reset complete");
#endif
#if defined(PANELAN_SC05X_MODE) && defined(PANELAN_LVGL_UI_MODE) && defined(PANELAN_MINI_SPI_MODE)
        StartupDiagnostics::mark(StartupDiagnostics::McpBegin, true);
#endif
        return true;
    }

    // No cached sample on failure: the caller must discard a broken press
    // sequence and re-arm only after an all-released observation.
    bool readSwitches(uint8_t &pressed) {
        if (!ready_) return false;
        for (uint8_t attempt = 0; attempt < 2; ++attempt) {
            if (attempt) {
                const uint32_t retryAt = micros();
                StartupDiagnostics::wireTiming(StartupDiagnostics::WireRetry, gpioA_, 0xff,
                                               attempt + 1, 0, retryAt, retryAt);
            }
            const uint32_t beginAt = micros();
            Wire.beginTransmission(address_);
            const uint32_t beginDone = micros();
            Wire.write(gpioA_);
            const uint32_t endAt = micros();
            const uint8_t status = Wire.endTransmission(false);
            const uint32_t endDone = micros();
            const uint32_t requestAt = micros();
            const uint8_t count = status == 0 ? Wire.requestFrom(address_, uint8_t(1)) : 0;
            const uint32_t requestDone = micros();
            if (status == 0 && count == 1 && Wire.available()) {
                pressed = static_cast<uint8_t>(~Wire.read());
                return true;
            }
            // Successful polling is frequent; retain only failures in the bounded ring.
            StartupDiagnostics::wireTiming(StartupDiagnostics::McpWrite, gpioA_, 0xff, attempt + 1,
                                           status, beginAt, beginDone, endAt, endDone);
            if (status == 0)
                StartupDiagnostics::wireTiming(StartupDiagnostics::WireRead, gpioA_, 0xff, count,
                                               status, endAt, endDone, requestAt, requestDone);
            StartupDiagnostics::wireError(gpioA_, 0xff, attempt + 1,
                                          status ? status : count ? 0xfe : 0xfd);
            restart(gpioA_, 0xff, attempt + 1);
        }
        return false;
    }

    // Always deselect all six before asserting one CS. No other SPI client
    // may use the shared bus while a selection is held.
    bool select(uint8_t display) {
        if (!ready_ || display < 1 || display > 6) {
            StartupDiagnostics::record(StartupDiagnostics::McpFault,
                ready_ ? StartupDiagnostics::InvalidSelect : StartupDiagnostics::NotReady, display, active_);
            return false;
        }
        if (active_) StartupDiagnostics::record(StartupDiagnostics::McpFault, StartupDiagnostics::Overlap, display, active_);
        StartupDiagnostics::record(StartupDiagnostics::McpIntent, 2, 0x80, high_);
        if (!write(olatB_, high_)) {
            StartupDiagnostics::record(StartupDiagnostics::McpFault, StartupDiagnostics::SelectFailed, display, active_);
            return false;
        }
        active_ = 0;
        probeAfterRecoveredHigh();
        const uint8_t selected = high_ & ~(1u << (display - 1));
        StartupDiagnostics::record(StartupDiagnostics::McpIntent, 3, 0x80, selected, display);
        if (!write(olatB_, selected)) {
            StartupDiagnostics::record(StartupDiagnostics::McpFault, StartupDiagnostics::SelectFailed, display, active_);
            return false;
        }
        active_ = display;
        return true;
    }

    bool deselect() {
        if (!ready_) {
            StartupDiagnostics::record(StartupDiagnostics::McpFault, StartupDiagnostics::NotReady, 0, active_);
            return false;
        }
        if (!active_) StartupDiagnostics::record(StartupDiagnostics::McpFault, StartupDiagnostics::InvalidDeselect);
        StartupDiagnostics::record(StartupDiagnostics::McpIntent, 2, 0x80, high_);
        if (!write(olatB_, high_)) {
            StartupDiagnostics::record(StartupDiagnostics::McpFault, StartupDiagnostics::DeselectFailed, 0, active_);
            return false;
        }
        active_ = 0;
        probeAfterRecoveredHigh();
        return true;
    }
};
