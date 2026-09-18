#include "PanelLanDisplay.h"

#ifdef PANELAN_SC05X_MODE

void PanelLanDisplay::begin() {
    tft_.begin();
    // The controller will be installed with the long edge horizontal. LovyanGFX
    // applies this rotation to both the ST7789 frame buffer and FT5x06 touch
    // coordinates, so all UI hit targets below use the 320x240 landscape view.
    tft_.setRotation(1);
    tft_.fillScreen(TFT_BLACK);
    tft_.fillRect(0, 0, tft_.width(), 42, TFT_NAVY);
    tft_.setTextColor(TFT_WHITE, TFT_NAVY);
    tft_.setTextSize(2);
    tft_.setCursor(12, 13);
    tft_.print("IGNITRON");
    tft_.setTextSize(1);
    tft_.setTextColor(TFT_CYAN, TFT_NAVY);
    tft_.setCursor(204, 19);
    tft_.print("SPARK CONTROLLER");
    drawPresetButtons();
    initialized_ = true;
    drawConnectionCard(false);
}

void PanelLanDisplay::drawPresetButtons() {
    constexpr uint16_t buttonWidth = 70;
    constexpr uint16_t buttonHeight = 56;
    constexpr uint16_t firstButtonX = 10;
    constexpr uint16_t buttonGap = 7;
    constexpr uint16_t buttonY = 171;

    tft_.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft_.setTextSize(1);
    tft_.setCursor(12, 153);
    tft_.print("HARDWARE PRESETS");

    for (uint8_t preset = 1; preset <= 4; ++preset) {
        const uint16_t x = firstButtonX + (preset - 1) * (buttonWidth + buttonGap);
        tft_.fillRoundRect(x, buttonY, buttonWidth, buttonHeight, 7, TFT_DARKGREY);
        tft_.drawRoundRect(x, buttonY, buttonWidth, buttonHeight, 7, TFT_CYAN);
        tft_.setTextColor(TFT_WHITE, TFT_DARKGREY);
        tft_.setTextSize(2);
        tft_.setCursor(x + 25, buttonY + 9);
        tft_.print(preset);
        tft_.setTextSize(1);
        tft_.setCursor(x + 12, buttonY + 36);
        tft_.print("PRESET");
    }
}

void PanelLanDisplay::drawConnectionCard(bool sparkConnected) {
    const uint16_t accent = sparkConnected ? TFT_GREEN : TFT_ORANGE;
    tft_.fillRoundRect(10, 53, tft_.width() - 20, 82, 7, TFT_DARKGREY);
    tft_.drawRoundRect(10, 53, tft_.width() - 20, 82, 7, accent);
    tft_.fillCircle(24, 68, 5, accent);
    tft_.setTextColor(accent, TFT_DARKGREY);
    tft_.setTextSize(1);
    tft_.setCursor(36, 64);
    tft_.print(sparkConnected ? "CONNECTED" : "SEARCHING FOR SPARK");
    tft_.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft_.setCursor(18, 84);
    tft_.printf("%s", sparkConnected ? model_ : "Turn on your Spark device");
    tft_.setTextColor(TFT_LIGHTGREY, TFT_DARKGREY);
    tft_.setCursor(18, 106);
    if (sparkConnected && serial_[0]) {
        tft_.printf("Serial  %s", serial_);
    } else if (sparkConnected) {
        tft_.print("Reading device identity...");
    } else {
        tft_.print("Auto-reconnect is enabled");
    }
}

void PanelLanDisplay::setPresetCallback(bool (*callback)(uint8_t)) {
    presetCallback_ = callback;
}

void PanelLanDisplay::setSparkIdentity(const char *model, const char *serial) {
    if (strncmp(model_, model, sizeof(model_) - 1) == 0 &&
        strncmp(serial_, serial, sizeof(serial_) - 1) == 0) {
        return;
    }
    strncpy(model_, model, sizeof(model_) - 1);
    model_[sizeof(model_) - 1] = '\0';
    strncpy(serial_, serial, sizeof(serial_) - 1);
    serial_[sizeof(serial_) - 1] = '\0';
    identityChanged_ = true;
}

void PanelLanDisplay::update(bool sparkConnected) {
    if (!initialized_) {
        return;
    }

    if (sparkConnected != lastConnectionState_) {
        lastConnectionState_ = sparkConnected;
        drawConnectionCard(sparkConnected);
    }

    if (identityChanged_) {
        identityChanged_ = false;
        drawConnectionCard(sparkConnected);
    }

    uint16_t x = 0;
    uint16_t y = 0;
    if (tft_.getTouch(&x, &y)) {
        constexpr uint16_t buttonWidth = 70;
        constexpr uint16_t buttonHeight = 56;
        constexpr uint16_t firstButtonX = 10;
        constexpr uint16_t buttonGap = 7;
        constexpr uint16_t buttonY = 171;
        // FT5x06 reports the same finger across many display frames.  A Spark
        // command must be sent once per tap, not once per frame while held.
        if (!touchActive_ && presetCallback_) {
            uint8_t preset = 0;
            if (y >= buttonY && y < buttonY + buttonHeight && x >= firstButtonX) {
                const uint8_t candidate = (x - firstButtonX) / (buttonWidth + buttonGap) + 1;
                const uint16_t candidateX = firstButtonX + (candidate - 1) * (buttonWidth + buttonGap);
                if (candidate <= 4 && x < candidateX + buttonWidth) {
                    preset = candidate;
                }
            }
            if (preset) {
                drawPresetFeedback(preset, presetCallback_(preset));
                touchActive_ = true;
                return;
            }
        }
        touchActive_ = true;
    } else {
        touchActive_ = false;
    }
}

void PanelLanDisplay::drawPresetFeedback(uint8_t preset, bool sent) {
    constexpr uint16_t buttonWidth = 70;
    constexpr uint16_t buttonHeight = 56;
    constexpr uint16_t buttonGap = 7;
    const uint16_t x = 10 + (preset - 1) * (buttonWidth + buttonGap);
    constexpr uint16_t y = 171;
    const uint16_t color = sent ? TFT_GREEN : TFT_RED;
    tft_.fillRoundRect(x, y, buttonWidth, buttonHeight, 7, color);
    tft_.setTextColor(TFT_WHITE, color);
    tft_.setTextSize(2);
    tft_.setCursor(x + 25, y + 9);
    tft_.print(preset);
    tft_.setTextSize(1);
    tft_.setCursor(x + 12, y + 36);
    tft_.print(sent ? "SENT" : "WAIT");
}

#endif
