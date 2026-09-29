#pragma once

#include <LovyanGFX.hpp>

// Prototype-only counterpart of PanelLanMiniSPI: same standalone ST7735S
// settings, except the proven prototype wiring swaps DC and CS. BLK is
// externally powered, not driven by the MCU.
class PanelLanPrototypeMiniSPI : public lgfx::LGFX_Device {
    lgfx::Panel_ST7735S panel_;
    lgfx::Bus_SPI bus_;

public:
    PanelLanPrototypeMiniSPI() {
        auto busConfig = bus_.config();
        busConfig.spi_host = SPI2_HOST;
        busConfig.spi_mode = 0;
        busConfig.freq_write = 10000000;
        busConfig.pin_mosi = 10;
        busConfig.pin_sclk = 11;
        busConfig.pin_miso = -1;
        busConfig.pin_dc = 13;
        bus_.config(busConfig);
        panel_.setBus(&bus_);

        auto panelConfig = panel_.config();
        panelConfig.pin_rst = 12;
        panelConfig.pin_cs = 14;
        panelConfig.panel_width = 80;
        panelConfig.panel_height = 160;
        panelConfig.offset_x = 26;
        panelConfig.offset_y = 1;
        panelConfig.readable = false;
        panel_.config(panelConfig);
        setPanel(&panel_);
    }
};
