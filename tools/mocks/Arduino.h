#pragma once
#include <cstdint>
#include <vector>
struct TestSerial { template<class... T> void printf(const char *, T...) {} void println(const char *) {} };
extern TestSerial Serial;
inline uint32_t testMillis = 0;
inline std::vector<unsigned> testDelays;
inline uint32_t millis() { return testMillis; }
inline uint32_t micros() { return testMillis * 1000; }
inline void delay(unsigned ms) { testDelays.push_back(ms); testMillis += ms; }
