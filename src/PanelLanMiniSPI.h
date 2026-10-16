#pragma once

#include <LovyanGFX.hpp>

// Shared write-only mini profile for standalone and integrated SPI tests.
// CS and reset are owned by PanelLanMiniMCP23017, never by LovyanGFX.
// BLK is externally powered; do not drive it from the MCU.
class PanelLanMiniSPI : public lgfx::LGFX_Device {
    lgfx::Panel_ST7735S panel_;
    lgfx::Bus_SPI bus_;

public:
    // Integrated landscape panels may need a distinct horizontal offset after
    // 90-degree rotation; preserve the standalone portrait default of one.
    explicit PanelLanMiniSPI(uint8_t landscapeXOffset = 1) {
        auto busConfig = bus_.config();
        // The legacy stage-2 A/B profile deliberately reproduces the former
        // SPI2 path. Normal firmware avoids that default path, which restarts
        // Arduino's global SPI bus (SPI.end()/begin()) during panel init/release.
#if defined(PANELAN_I2C_AB_LEGACY_SPI2)
        busConfig.spi_host = SPI2_HOST;
#else
        // Normal mini bus uses SPI3; pins, mode, and clock are unchanged.
        busConfig.spi_host = SPI3_HOST;
#endif
        busConfig.spi_mode = 0;
        busConfig.freq_write = 10000000;
        busConfig.pin_mosi = 10;
        busConfig.pin_sclk = 11;
        busConfig.pin_miso = -1;
        busConfig.pin_dc = 14;
        bus_.config(busConfig);
        panel_.setBus(&bus_);

        auto panelConfig = panel_.config();
        panelConfig.pin_rst = -1;
        panelConfig.pin_cs = -1;
        panelConfig.panel_width = 80;
        panelConfig.panel_height = 160;
        panelConfig.offset_x = 26;
        panelConfig.offset_y = landscapeXOffset;
        panelConfig.readable = false;
        panel_.config(panelConfig);
        setPanel(&panel_);
    }
};
