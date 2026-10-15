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

    bool write(uint8_t reg, uint8_t value) {
        for (uint8_t attempt = 0; attempt < 2; ++attempt) {
            Wire.beginTransmission(address_);
            Wire.write(reg);
            Wire.write(value);
            const uint8_t status = Wire.endTransmission();
            if (status == 0) return true;
            if (attempt == 1) {
                Serial.printf("MCP23017: write reg 0x%02X failed (I2C status %u)\n", reg, status);
                return false;
            }

            // SPI/display initialization can leave the shared Wire peripheral
            // needing a restart. Rebind only after a failed transaction, then
            // retry once; successful transactions keep the bus untouched.
            Wire.end();
            delay(2);
            Wire.begin(12, 13);
        }
        return false;
    }

public:
    bool begin() {
#if defined(PANELAN_SC05X_MODE) && defined(PANELAN_LVGL_UI_MODE) && defined(PANELAN_MINI_SPI_MODE)
        StartupDiagnostics::mark(StartupDiagnostics::McpBegin, false);
#endif
        ready_ = false;
        Wire.begin(12, 13); // IO12 SDA, IO13 SCL (not TFT RES/CS)
        // Preload the latch *while pins are still inputs*: no selected TFT
        // when switching GPB0..6 to outputs. GPB7 is left as an input.
        if (!write(olatB_, high_) || !write(iodirB_, 0x80) ||
            !write(iodirA_, 0xff) || !write(gppuA_, 0xff)) {
            Serial.println("MCP23017: no ACK at 0x27 or port-B setup failed");
            return false;
        }
        ready_ = true;
        delay(10);
        if (!write(olatB_, csMask_)) { ready_ = false; return false; }
        delay(20);
        if (!write(olatB_, high_)) { ready_ = false; return false; }
        delay(120);
        Serial.println("MCP23017: detected at 0x27; GPB0-GPB6 ready, shared TFT1..TFT6 reset complete");
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
            Wire.beginTransmission(address_);
            Wire.write(gpioA_);
            if (Wire.endTransmission(false) == 0 && Wire.requestFrom(address_, uint8_t(1)) == 1 && Wire.available()) {
                pressed = static_cast<uint8_t>(~Wire.read());
                return true;
            }
            Wire.end();
            delay(2);
            Wire.begin(12, 13);
        }
        return false;
    }

    // Always deselect all six before asserting one CS. No other SPI client
    // may use the shared bus while a selection is held.
    bool select(uint8_t display) {
        if (!ready_ || display < 1 || display > 6) return false;
        if (!write(olatB_, high_) || !write(olatB_, high_ & ~(1u << (display - 1)))) {
            return false;
        }
        return true;
    }

    bool deselect() {
        if (!ready_) return false;
        if (!write(olatB_, high_)) return false;
        return true;
    }
};
