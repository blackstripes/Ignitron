#pragma once

#include <Arduino.h>

#ifdef PANELAN_SC05X_MODE

#include <PanelLan.h>

// Landscape player UI for the PanelLan SC05_X board. It intentionally knows
// nothing about Spark protocol state: it keeps board-specific display/touch
// code isolated while the existing serial CLI remains the controller interface.
class PanelLanDisplay {
public:
    void begin();
    void update(bool sparkConnected);
    void setPresetCallback(bool (*callback)(uint8_t));
    void setSparkIdentity(const char *model, const char *serial);

private:
    PanelLan tft_{BOARD_SC05_X};
    bool initialized_ = false;
    bool lastConnectionState_ = false;
    bool touchActive_ = false;
    bool (*presetCallback_)(uint8_t) = nullptr;
    char model_[32] = "Identifying...";
    char serial_[32] = {};
    bool identityChanged_ = true;

    void drawConnectionCard(bool sparkConnected);
    void drawPresetButtons();
    void drawPresetFeedback(uint8_t preset, bool sent);
};

#endif
