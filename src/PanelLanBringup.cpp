#include <Arduino.h>
#include <PanelLan.h>

PanelLan tft(BOARD_SC05_X);

void setup() {
    Serial.begin(115200);
    const unsigned long serialReadyDeadline = millis() + 1500;
    while (!Serial && millis() < serialReadyDeadline) {
        delay(10);
    }

    Serial.println("PanelLan SC05_X display bring-up");
    tft.begin();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(18, 24);
    tft.print("IGNITRON");
    tft.setTextSize(1);
    tft.setCursor(18, 58);
    tft.print("SC05_X display OK");
    tft.setCursor(18, 76);
    tft.print("Touch anywhere to test");
    Serial.println("Display initialized; touch the panel.");
}

void loop() {
    uint16_t x = 0;
    uint16_t y = 0;
    if (tft.getTouch(&x, &y)) {
        tft.fillCircle(x, y, 4, TFT_CYAN);
        Serial.printf("Touch: %u, %u\n", x, y);
        delay(80);
    }
}
