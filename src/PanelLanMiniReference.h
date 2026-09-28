#pragma once
#include <stdint.h>

// Diagnostic protocol generator, independent of graphics state and SPI/DMA.
// Writer provides command(byte), data(byte), pause(ms). No panel readback.
namespace PanelLanMiniReference {
template <class Writer> void replayInitList(Writer &wire, const uint8_t *list) {
    for (;;) {
        const uint8_t command = *list++;
        const uint8_t count = *list++;
        if (command == 0xFF && count == 0xFF) return;
        wire.command(command);
        for (unsigned i = 0; i < (count & 0x7F); ++i) wire.data(*list++);
        if (count & 0x80) {
            const unsigned ms = *list++;
            wire.pause(ms == 255 ? 500 : ms);
        }
    }
}

template <class Writer> void frame(Writer &wire) {
    // Explicit 16-bit format/rotation and the entire RAM, not a cached 80x160
    // glass window. Matches LGFX's rotation-0 MADCTL BGR and RGB565 COLMOD.
    wire.command(0x3A); wire.data(0x55);
    wire.command(0x36); wire.data(0x08);
    wire.command(0x2A);
    wire.data(0); wire.data(0); wire.data(0); wire.data(131);
    wire.command(0x2B);
    wire.data(0); wire.data(0); wire.data(0); wire.data(161);
    wire.command(0x2C);
    constexpr uint16_t colors[] = {0xFFFF, 0xF800, 0x07E0, 0x001F, 0xFFE0, 0x07FF};
    for (unsigned y = 0; y < 162; ++y) {
        const uint16_t color = colors[y / 27];
        for (unsigned x = 0; x < 132; ++x) {
            wire.data(color >> 8);
            wire.data(color & 0xFF);
        }
        // CS remains asserted, clock low. Let BLE/USB/RTOS run between rows.
        wire.pause(1);
    }
}
}
