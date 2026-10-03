#pragma once

#include <Arduino.h>
#include <Wire.h>

// MCP23017 at 0x20, default BANK=0 register map. Only port B is driven:
// GPB0..5 = active-low TFT1..6 CS; GPB6 = shared active-low reset.
// GPB7 remains an input. No dependency on the main PanelLan touch I2C bus.
class PanelLanMiniMCP23017 {
    static constexpr uint8_t address_ = 0x20;
    static constexpr uint8_t csMask_ = 0x3f;
    static constexpr uint8_t resetMask_ = 0x40;
    static constexpr uint8_t high_ = csMask_ | resetMask_;
    static constexpr uint8_t olatB_ = 0x15;
    static constexpr uint8_t iodirB_ = 0x01;
    bool ready_ = false;

    bool write(uint8_t reg, uint8_t value) {
        Wire.beginTransmission(address_);
        Wire.write(reg);
        Wire.write(value);
        return Wire.endTransmission() == 0;
    }

public:
    bool begin() {
        ready_ = false;
        Wire.begin(12, 13); // IO12 SDA, IO13 SCL (not TFT RES/CS)
        // Preload the latch *while pins are still inputs*: no selected TFT
        // when switching GPB0..6 to outputs. GPB7 is left as an input.
        if (!write(olatB_, high_) || !write(iodirB_, 0x80)) {
            Serial.println("MCP23017: no ACK at 0x20 or port-B setup failed");
            return false;
        }
        ready_ = true;
        delay(10);
        if (!write(olatB_, csMask_)) { ready_ = false; return false; }
        delay(20);
        if (!write(olatB_, high_)) { ready_ = false; return false; }
        delay(120);
        Serial.println("MCP23017: detected at 0x20; GPB0-GPB6 ready, TFT1 reset complete");
        return true;
    }

    // Always deselect all six before asserting one CS. No other SPI client
    // may use the shared bus while a selection is held.
    bool select(uint8_t display) {
        if (!ready_ || display < 1 || display > 6) return false;
        if (!write(olatB_, high_) || !write(olatB_, high_ & ~(1u << (display - 1)))) {
            ready_ = false;
            return false;
        }
        return true;
    }

    bool deselect() {
        if (!ready_) return false;
        if (!write(olatB_, high_)) { ready_ = false; return false; }
        return true;
    }
};
