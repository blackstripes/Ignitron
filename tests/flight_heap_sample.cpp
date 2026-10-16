#include "../src/FlightHeapSample.h"
#include <cassert>
#include <cstdint>

int main() {
    using FlightHeapSample::pack;
    assert(pack(0, 0) == 0);
    assert(pack(4095, 4095) == 0);
    assert(pack(4096, 8192) == 0x0201);
    assert(pack(255 * 4096u + 4095, 1) == 0x00ff);
    assert(pack(UINT32_MAX, UINT32_MAX) == 0xffff);
    assert(pack(8192, 4096) == 0x0102);
}
