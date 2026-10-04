#include "PanelLanMiniDisplay.h"

#if defined(PANELAN_SC05X_MODE) && defined(PANELAN_LVGL_UI_MODE) && defined(PANELAN_MINI_SPI_MODE)
#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include "PanelLanMiniSPI.h"
#include "PanelLanMiniMCP23017.h"
#include "controller/HardwarePresetNames.h"

namespace {
// Rotation maps the portrait row offset onto landscape X. The integrated
// module showed a stray left-edge RAM column at offset 1; start at column 0.
PanelLanMiniSPI tft1(0);
PanelLanMiniSPI tft2(0);
PanelLanMiniSPI *const tfts[] = {&tft1, &tft2};
PanelLanMiniMCP23017 miniSelect;

void drawSplash(PanelLanMiniSPI &tft) {
    constexpr uint16_t colors[] = {0xF800, 0x07E0, 0x001F, 0xFFE0, 0xF81F, 0x07FF};
    tft.fillScreen(0x0000);
    for (int i = 0; i < 6; ++i) tft.fillRect(4 + i * 26, 4, 26, 72, colors[i]);
    tft.drawRect(0, 0, 160, 80, 0xFFFF);
    tft.drawRect(2, 2, 156, 76, 0x0000);
    tft.setTextSize(2);
    tft.setTextColor(0xFFFF, 0x0000);
    tft.setCursor(72, 32);
    tft.print("MINI");
}

constexpr uint16_t bg = 0x0021;
constexpr uint16_t white = 0xFFFF;
constexpr uint16_t muted = 0x7BD0;
constexpr uint16_t green = 0x07EC;
// Match the main FX page accents: GATE #00E65D and COMP #48D8BC,
// converted to RGB565 for the mini TFTs.
constexpr uint16_t gateGreen = 0x072B;
constexpr uint16_t compTeal = 0x4ED7;
constexpr uint16_t amber = 0xFEA0;
constexpr uint16_t red = 0xF924;

enum class Layout : uint8_t { Preset, Gate, Other };
enum class Mark : uint8_t { None, Dots, Bang, Dash, Wave };
struct Card {
    char name[48]{};
    char status[20]{};
    uint16_t accent = amber;
    Layout layout = Layout::Other;
    Mark mark = Mark::Dash;
    bool filled = false;
};

// These built-in fonts cover ASCII. Collapse an unsupported UTF-8 codepoint
// to one '?' rather than silently joining the surrounding words.
void shortName(char *out, size_t size, const std::string &name) {
    size_t n = 0;
    for (unsigned char c : name) {
        if (c < 32 || (c >= 0x80 && c < 0xC0)) continue;
        if (n == size - 1) {
            // Preserve an explicit truncation cue even before pixel fitting.
            if (n >= 3) memcpy(out + n - 3, "...", 3);
            break;
        }
        out[n++] = c < 127 ? static_cast<char>(c) : '?';
    }
    out[n] = '\0';
}

// Two-pixel waveform from miniUI / miniUI2: a quiet baseline with three
// narrow peaks, not a generic diamond or a large decorative graph.
void waveform(lgfx::LGFXBase &g, int x, int y, uint16_t color, int height = 30) {
    constexpr int8_t points[][2] = {
        {0, 15}, {6, 15}, {9, 7}, {12, 23}, {16, 0},
        {19, 29}, {23, 5}, {26, 22}, {30, 11}, {33, 15}, {39, 15}
    };
    for (size_t i = 1; i < sizeof(points) / sizeof(points[0]); ++i) {
        for (int dx = 0; dx < 2; ++dx)
            g.drawLine(x + points[i-1][0] + dx, y + points[i-1][1] * (height - 1) / 29,
                       x + points[i][0] + dx, y + points[i][1] * (height - 1) / 29, color);
    }
}

// Fit by measured pixel width. Keep short names big; truncate only after
// stepping down to the still-readable 12pt face.
void nameLine(lgfx::LGFXBase &g, const char *name, int centerY, uint16_t color) {
    char text[sizeof(Card::name)];
    snprintf(text, sizeof(text), "%s", name);
    g.setFont(&fonts::FreeSansBold18pt7b);
    if (g.textWidth(text) > 140) g.setFont(&fonts::FreeSansBold12pt7b);
    size_t len = strlen(text);
    if (g.textWidth(text) > 140) {
        do {
            text[--len] = '\0';
        } while (len && g.textWidth(text) + g.textWidth("...") > 140);
        strcpy(text + len, "...");
    }
    g.setTextColor(color, bg);
    g.drawString(text, 80, centerY);
}

void drawMark(lgfx::LGFXBase &g, Mark mark, int y, uint16_t color) {
    switch (mark) {
    case Mark::Wave: waveform(g, 60, y - 7, color, 14); break;
    case Mark::Dots:
        for (int x = 68; x <= 92; x += 12) g.fillCircle(x, y, 2, color);
        break;
    case Mark::Bang:
        g.setFont(&fonts::FreeSansBold12pt7b);
        g.setTextColor(color);
        g.drawString("!", 80, y + 1);
        break;
    case Mark::Dash: g.fillRoundRect(69, y - 1, 22, 3, 1, color); break;
    case Mark::None: break;
    }
}

void drawCard(PanelLanMiniSPI &g, const Card &card) {
    g.fillScreen(bg);
    g.setTextSize(1);
    g.setTextDatum(lgfx::textdatum_t::middle_center);
    g.setTextWrap(false);
    g.drawRoundRect(2, 2, 156, 76, 4, card.accent);
    g.drawRoundRect(3, 3, 154, 74, 3, card.accent);
    if (card.layout == Layout::Gate) {
        nameLine(g, card.name, 21, white);
    } else {
        nameLine(g, card.name, 27, card.filled ? white : card.accent);
    }
    const int barY = card.layout == Layout::Gate ? 40 : 49;
    const int barH = card.layout == Layout::Gate ? 36 : 26;
    if (card.filled) g.fillRoundRect(7, barY, 146, barH, 3, card.accent);
    else g.drawRoundRect(7, barY, 146, barH, 3, card.accent);
    const uint16_t ink = card.filled ? bg : card.accent;
    if (card.status[0]) {
        g.setFont(card.layout == Layout::Gate ? &fonts::FreeSansBold18pt7b : &fonts::FreeSansBold12pt7b);
        if (g.textWidth(card.status) > 138) g.setFont(&fonts::FreeSansBold9pt7b);
        g.setTextColor(ink);
        g.drawString(card.status, 80, barY + barH / 2);
    } else drawMark(g, card.mark, barY + barH / 2, ink);
}
} // namespace

PanelLanMiniDisplay::PanelLanMiniDisplay() = default;

void PanelLanMiniDisplay::initPanels() {
    while (miniInitialized_ < 2) {
        const uint8_t display = miniInitialized_ + 1;
        if (!miniSelect.select(display)) {
            // A failed selection may have left a CS asserted. Try to clear it;
            // the next select also starts with all CS high.
            miniSelect.deselect();
            miniRetryAt_ = millis() + 1000;
            Serial.printf("ST7735S: MCP23017 select failed; TFT%u init will retry\n", display);
            return;
        }
        PanelLanMiniSPI &tft = *tfts[miniInitialized_];
        tft.init();
        tft.setRotation(1); // 90 degrees clockwise: 160x80 landscape
        Serial.printf("ST7735S TFT%u logical geometry: %d x %d\n", display, tft.width(), tft.height());
        drawSplash(tft); // first pixels before controller state is consulted
        if (!miniSelect.deselect()) {
            miniRetryAt_ = millis() + 1000;
            Serial.printf("ST7735S: MCP23017 deselect failed; TFT%u init will retry\n", display);
            return;
        }
        Serial.printf("ST7735S TFT%u: SPI2 color bars sent (no readback)\n", display);
        ++miniInitialized_;
    }
    splashAt_ = millis();
}

void PanelLanMiniDisplay::begin() {
    Serial.println("ST7735S: integrated SPI2 init (write-only)");
    if (!miniSelect.begin()) {
        Serial.println("ST7735S: MCP23017 init failed; minis disabled");
        return;
    }
    miniReady_ = true;
    initPanels();
}

void PanelLanMiniDisplay::update(const ControllerSnapshot &snapshot, PanelLanLVGLUI::View view) {
    if (!miniReady_) return;
    const uint32_t now = millis();
    if (static_cast<int32_t>(now - miniRetryAt_) < 0) return;
    if (miniInitialized_ < 2) {
        initPanels();
        return;
    }
    // Keep the diagnostic visible through startup, even if the controller is
    // disconnected or BLE setup is slow. Afterwards use only SPI2 for state cards.
    if (now - splashAt_ < 5000) return;
    for (uint8_t i = 0; i < 2; ++i) {
        const uint8_t display = i + 1;
        Card card;
        switch (view) {
        case PanelLanLVGLUI::View::Preset:
            card.layout = Layout::Preset;
            // Slot identity is independent of the active selection and its color.
            shortName(card.name, sizeof(card.name), HardwarePresetNames::label(snapshot.hardwarePresetNames, display));
            if (!card.name[0]) strcpy(card.name, "UNKNOWN");
            if (snapshot.pendingHardwarePreset) { card.accent = amber; card.mark = Mark::Dots; }
            else if (snapshot.presetActionFailed) { card.accent = red; card.mark = Mark::Bang; }
            else if (snapshot.sparkStateStale || !snapshot.confirmedHardwarePreset) {
                card.accent = amber;
            } else if (snapshot.confirmedHardwarePreset == display) {
                card.accent = amber; card.filled = true; card.mark = Mark::Wave;
            } else { card.accent = muted; card.mark = Mark::Dash; }
            break;
        case PanelLanLVGLUI::View::Fx: {
            card.layout = Layout::Gate;
            strcpy(card.name, i == 0 ? "GATE" : "COMP");
            const ControllerFxSlot &slot = snapshot.fxSlots[i];
            const uint16_t fxAccent = i == 0 ? gateGreen : compTeal;
            if (slot.pending) { card.accent = amber; card.mark = Mark::Dots; }
            else if (slot.actionFailed) { card.accent = red; card.mark = Mark::Bang; }
            else if (snapshot.sparkStateStale || !slot.known) card.accent = amber;
            else {
                strcpy(card.status, slot.enabled ? "ON" : "OFF");
                card.accent = slot.enabled ? fxAccent : muted;
                card.filled = true;
            }
            break;
        }
        case PanelLanLVGLUI::View::Looper:
            strcpy(card.name, "LOOPER"); strcpy(card.status, "UNWIRED");
            card.accent = muted; break;
        case PanelLanLVGLUI::View::Tuner:
            strcpy(card.name, "TUNER");
            if (!snapshot.tunerActive) card.accent = muted;
            else if (snapshot.sparkStateStale || !snapshot.tunerSampleFresh || !snapshot.tunerSampleKnown) card.accent = amber;
            else if (snapshot.tunerNote.empty() || snapshot.tunerNote == " ") card.accent = muted;
            else {
                char note[12]{};
                shortName(note, sizeof(note), snapshot.tunerNote);
                note[4] = '\0';
                if (note[0]) {
                    snprintf(card.name, sizeof(card.name), "%s", note);
                    snprintf(card.status, sizeof(card.status), "%+dc", snapshot.tunerOffsetCents);
                    card.accent = green; card.filled = true;
                }
            }
            break;
        case PanelLanLVGLUI::View::Device:
            strcpy(card.name, "SPARK");
            switch (snapshot.connectionPhase) {
            case ControllerConnectionPhase::Scanning: strcpy(card.status, "SEARCHING"); break;
            case ControllerConnectionPhase::Reconnecting: strcpy(card.status, "LINKING"); break;
            case ControllerConnectionPhase::Identifying:
            case ControllerConnectionPhase::Syncing: card.mark = Mark::Dots; break;
            case ControllerConnectionPhase::Ready:
                if (!snapshot.sparkStateStale) {
                    strcpy(card.status, "LINKED"); card.accent = green; card.filled = true;
                }
                break;
            }
            break;
        }
        char key[sizeof(lastCard_[i])];
        // Include all visible fields. Length prefixes prevent delimiter collisions
        // in Spark-provided names; every field fits in the mini-only cache buffer.
        snprintf(key, sizeof(key), "%02u:%s|%s|%u|%u|%u|%u", static_cast<unsigned>(strlen(card.name)),
                 card.name, card.status, card.accent, static_cast<unsigned>(card.layout),
                 static_cast<unsigned>(card.mark), card.filled);
        if (cardDrawn_[i] && view == lastView_[i] && !strcmp(key, lastCard_[i])) continue;
        if (!miniSelect.select(display)) {
            miniSelect.deselect();
            miniRetryAt_ = millis() + 1000;
            Serial.printf("ST7735S: MCP23017 select failed; TFT%u draw will retry\n", display);
            return;
        }
        drawCard(*tfts[i], card);
        if (!miniSelect.deselect()) {
            miniRetryAt_ = millis() + 1000;
            Serial.printf("ST7735S: MCP23017 deselect failed; TFT%u draw will retry\n", display);
            return;
        }
        lastView_[i] = view;
        strcpy(lastCard_[i], key);
        cardDrawn_[i] = true;
        if (view == PanelLanLVGLUI::View::Preset)
            Serial.printf("ST7735S TFT%u preset card sent (no readback): slot=%u name=%s active=%u\n",
                          display, display, card.name, snapshot.confirmedHardwarePreset);
    }
}
#endif
