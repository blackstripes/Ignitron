#pragma once
#include <cstdint>
struct TestSerial { template<class... T> void printf(const char *, T...) {} void println(const char *) {} };
extern TestSerial Serial;
inline void delay(unsigned) {}
