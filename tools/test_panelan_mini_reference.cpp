// Host-only protocol test: c++ -std=c++11 -Wall -Wextra -Werror
// tools/test_panelan_mini_reference.cpp -o /tmp/opencode/test-mini-reference
#include "../src/PanelLanMiniReference.h"
#include <cassert>
#include <cstdio>
#include <vector>

struct Event { char kind; unsigned value; };
struct Wire {
    std::vector<Event> events;
    uint32_t bytes = 0, hash = 2166136261u;
    void byte(char kind, uint8_t value) {
        events.push_back({kind, value});
        ++bytes;
        hash = (hash ^ value) * 16777619u;
    }
    void command(uint8_t value) { byte('C', value); }
    void data(uint8_t value) { byte('D', value); }
    void pause(unsigned ms) { events.push_back({'P', ms}); }
};

int main() {
    Wire init;
    const uint8_t list[] = {0x01, 0x80, 150, 0x11, 0x80, 255,
                            0xB1, 3, 1, 0x2C, 0x2D, 0xFF, 0xFF};
    PanelLanMiniReference::replayInitList(init, list);
    const Event expectedInit[] = {{'C',1}, {'P',150}, {'C',0x11}, {'P',500},
                                  {'C',0xB1}, {'D',1}, {'D',0x2C}, {'D',0x2D}};
    assert(init.events.size() == sizeof(expectedInit) / sizeof(Event));
    for (unsigned i = 0; i < init.events.size(); ++i) {
        assert(init.events[i].kind == expectedInit[i].kind);
        assert(init.events[i].value == expectedInit[i].value);
    }
    Wire frame;
    PanelLanMiniReference::frame(frame);
    const Event prefix[] = {{'C',0x3A}, {'D',0x55}, {'C',0x36}, {'D',8},
                            {'C',0x2A}, {'D',0}, {'D',0}, {'D',0}, {'D',131},
                            {'C',0x2B}, {'D',0}, {'D',0}, {'D',0}, {'D',161}, {'C',0x2C}};
    unsigned pos = 0;
    for (const auto &event : prefix) {
        assert(frame.events[pos].kind == event.kind);
        assert(frame.events[pos++].value == event.value);
    }
    const uint16_t colors[] = {0xFFFF, 0xF800, 0x07E0, 0x001F, 0xFFE0, 0x07FF};
    for (unsigned y = 0; y < 162; ++y) {
        for (unsigned x = 0; x < 132; ++x) {
            assert(frame.events[pos].kind == 'D');
            assert(frame.events[pos++].value == (colors[y / 27] >> 8));
            assert(frame.events[pos].kind == 'D');
            assert(frame.events[pos++].value == (colors[y / 27] & 255));
        }
        assert(frame.events[pos].kind == 'P');
        assert(frame.events[pos++].value == 1);
    }
    assert(pos == frame.events.size());
    assert(frame.bytes == 42783);
    std::printf("PASS: init delays/sentinel, full-RAM window, MSB-first RGB565, all 21384 pixels; raw-frame bytes=%u fnv1a=%u\n",
                frame.bytes, frame.hash);
}
