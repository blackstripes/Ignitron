#pragma once
#include <cstdint>
#include <vector>
struct TestWire {
    struct Write { uint8_t reg, value; };
    std::vector<Write> writes;
    uint8_t reg = 0, value = 0, gpio = 0xff;
    bool fail = false;
    int calls = 0;
    bool begin(int, int) { return true; } bool end() { return true; }
    void beginTransmission(uint8_t address) { if (address != 0x27) fail = true; calls = 0; }
    void write(uint8_t byte) { if (!calls++) reg = byte; else value = byte; }
    uint8_t endTransmission(bool = true) { if (fail) return 4; if (calls == 2) writes.push_back({reg, value}); return 0; }
    uint8_t requestFrom(uint8_t, uint8_t, uint8_t = 1) { return fail ? 0 : 1; }
    int available() { return fail ? 0 : 1; }
    int read() { return gpio; }
};
extern TestWire Wire;
