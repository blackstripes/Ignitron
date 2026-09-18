#pragma once

#if defined(PANELAN_SC05X_MODE) && defined(PANELAN_LVGL_UI_MODE)

#include <Arduino.h>
#include <PanelLan.h>
#include <lvgl.h>

#include "controller/ControllerState.h"

class ControllerActions;

// Presentation-only LVGL surface for the first BLE coexistence checkpoint.
// It accepts immutable snapshots from the application loop and sends no Spark
// commands. ControllerActions will replace that boundary before controls are
// added to this UI.
class PanelLanLVGLUI {
public:
    void begin();
    void update(const ControllerSnapshot &snapshot);
    void setActions(ControllerActions *actions) { actions_ = actions; }
    bool writeScreenshot(Stream &output);
    void injectTouch(uint16_t x, uint16_t y);

private:
    static constexpr uint16_t kDisplayWidth = 320;
    static constexpr uint16_t kDisplayHeight = 240;
    static constexpr uint16_t kBufferLines = 40;

    PanelLan tft_{BOARD_SC05_X};
    // This global UI object is statically allocated, so the partial buffer is
    // in internal RAM for this target. DMA_ATTR is not valid on C++ members.
    uint16_t drawBuffer_[kDisplayWidth * kBufferLines]{};
    lv_display_t *display_ = nullptr;
    lv_obj_t *connectionLabel_ = nullptr;
    lv_obj_t *identityLabel_ = nullptr;
    lv_obj_t *presetNameLabel_ = nullptr;
    lv_obj_t *presetMetaLabel_ = nullptr;
    lv_obj_t *presetDescriptionLabel_ = nullptr;
    lv_obj_t *actionStatusLabel_ = nullptr;
    lv_obj_t *fxTiles_[6]{};
    lv_obj_t *fxStateLabels_[6]{};
    lv_obj_t *presetPicker_ = nullptr;
    lv_obj_t *presetButtons_[4]{};
    ControllerActions *actions_ = nullptr;
    uint32_t renderedRevision_ = UINT32_MAX;
    uint32_t lastLvglTickAt_ = 0;
    bool injectedTouchPending_ = false;
    bool injectedTouchPressed_ = false;
    uint16_t injectedTouchX_ = 0;
    uint16_t injectedTouchY_ = 0;

    static void flushDisplay(lv_display_t *display, const lv_area_t *area, uint8_t *pixelMap);
    static void readTouch(lv_indev_t *, lv_indev_data_t *data);
    static void onPresetClicked(lv_event_t *event);
    static void onPresetCardClicked(lv_event_t *event);
    void createUi();
    void renderStatus(const ControllerSnapshot &snapshot);
};

#endif
