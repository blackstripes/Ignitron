#include <Arduino.h>
#include <LovyanGFX.hpp>
#include "PanelLanMiniSPI.h"
#include "PanelLanMiniMCP23017.h"

namespace {

// Standalone external TFT only. Do not instantiate PanelLan or drive BLK.
PanelLanMiniSPI tft;
PanelLanMiniMCP23017 miniSelect;

void drawDiagnostic() {
    constexpr uint16_t colors[] = {0xF800, 0x07E0, 0x001F, 0xFFE0, 0xF81F, 0x07FF};
    tft.fillScreen(0x0000);
    // Each bar has a fixed RGB565 color; missing/clipped edges reveal offsets.
    for (int i = 0; i < 6; ++i) {
        tft.fillRect(4, 4 + i * 24, 72, 24, colors[i]);
    }
    tft.drawRect(0, 0, 80, 160, 0xFFFF);
    tft.drawRect(2, 2, 76, 156, 0x0000);
    tft.setTextSize(1);
    tft.setTextColor(0xFFFF, 0x0000);
    tft.setCursor(5, 147);
    tft.print("ST7735S OK");
}

}  // namespace

void setup() {
    Serial.begin(115200);
    const uint32_t deadline = millis() + 1500;
    while (!Serial && millis() < deadline) {
        delay(10);
    }
    Serial.println("PanelLan SC05_X external ST7735S bring-up (no LCD/Spark)");
    Serial.println("3.3V external power for VCC and BLK; MOSI=10 SCK=11 DC=14; I2C SDA=12 SCL=13; MCP=0x27 GPB0=CS GPB6=RES; no MISO");
    Serial.println("Trying 80x160, RAM offset (26,1), rotation 0, 10 MHz SPI");
    if (!miniSelect.begin() || !miniSelect.select(1)) {
        Serial.println("MCP23017 init/select failed; diagnostic not sent");
        return;
    }
    tft.init();
    tft.setRotation(0);  // 80x160 portrait; orientation may vary by module
    Serial.printf("LovyanGFX geometry: %d x %d\n", tft.width(), tft.height());
    drawDiagnostic();
    if (!miniSelect.deselect()) {
        Serial.println("MCP23017 deselect failed");
        return;
    }
    Serial.println("Diagnostic sent: white/black border, RGB + yellow/magenta/cyan bars, ST7735S OK");
    Serial.println("No MISO/readback: verify colors, corners and text visually.");
}

void loop() {
    delay(1000);
}
