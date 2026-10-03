#pragma once

#if defined(PANELAN_SC05X_MODE) && defined(PANELAN_LVGL_UI_MODE)
#include <LovyanGFX.hpp>
#include "PanelLanLVGLUI.h"

#if defined(PANELAN_MINI_SPI_MODE) == defined(PANELAN_MINI_GPIO_MODE)
#error "Select exactly one integrated mini backend: SPI or GPIO"
#endif
#if defined(PANELAN_MINI_SPI_MODE) && defined(PANELAN_MINI_COEXISTENCE_TRACE)
#error "SPI mini and raw GPIO trace cannot share the mini pins"
#endif

// Direct, write-only top-left card; no LVGL objects or controller actions.
class PanelLanMiniDisplay {
public:
    PanelLanMiniDisplay();
    void begin();
    void update(const ControllerSnapshot &snapshot, PanelLanLVGLUI::View view);
#ifdef PANELAN_MINI_COEXISTENCE_TRACE
    // Register observations only: these cannot confirm pixels on a write-only LCD.
    static void traceHardware(const char *checkpoint, bool force = false);
    void diagnosticCommand(const String &args);
    bool diagnosticRunController() const { return runController_; }
    bool diagnosticServiceMain() const { return serviceMain_; }
#endif

private:
#ifdef PANELAN_MINI_SPI_MODE
    bool miniReady_ = false;
    uint32_t splashAt_ = 0;
    PanelLanLVGLUI::View lastView_{};
    char lastCard_[96]{};
    bool cardDrawn_ = false;
#else
    struct Panel : lgfx::Panel_ST7735S {
        using lgfx::Panel_ST7735S::getInitCommands;
    } panel_;
#ifdef PANELAN_MINI_COEXISTENCE_TRACE
    bool holdMini_ = false;
    bool runController_ = true;
    bool serviceMain_ = false;
    const char *diagnosticMode_ = "boot";
    const char *diagnosticTransport_ = "gpio-mode0";
    uint32_t diagnosticSequence_ = 0;
    uint32_t rawBytes_ = 0;
    uint32_t rawHash_ = 0;
    void referenceFrame(bool reset);
    void diagnosticStatus() const;
#endif
    // One visible row at a time; no 132x162 RAM sweep or LVGL allocation.
    uint16_t row_ = 0;
    bool sending_ = false;
    char title_[16]{};
    char status_[24]{};
    char footer_[24]{};
    uint16_t accent_ = 0;
    char lastTitle_[16]{};
    char lastStatus_[24]{};
    char lastFooter_[24]{};
    uint16_t lastAccent_ = 0;
    bool drawn_ = false;
    uint32_t lastDrawAt_ = 0;
#endif
};
#endif
