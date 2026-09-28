#include "../src/SparkMessageSequence.h"

#include <cassert>

int main() {
    assert(nextNormalSparkMessageNumber(0) == 2);
    assert(nextNormalSparkMessageNumber(1) == 2);
    assert(nextNormalSparkMessageNumber(0xEC) == 0xED);
    assert(nextNormalSparkMessageNumber(0xED) == 0xEF);
    assert(nextNormalSparkMessageNumber(0xFE) == 0xFF);
    assert(nextNormalSparkMessageNumber(0xFF) == 0);
    uint8_t next = 1;
    for (int i = 0; i < 1024; ++i) {
        assert(next != 0xEE);
        next = nextNormalSparkMessageNumber(next);
    }
}
