#include "PanelLanMiniDisplay.h"

#if defined(PANELAN_SC05X_MODE) && defined(PANELAN_LVGL_UI_MODE) && defined(PANELAN_MINI_SPI_MODE)
#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include "PanelLanMiniSPI.h"
#include "PanelLanMiniMCP23017.h"
#include "StartupDiagnostics.h"
#include "controller/HardwarePresetNames.h"
#include "controller/PhysicalPresetLayout.h"
#include "controller/MiniActionPresentation.h"
#if defined(PANELAN_I2C_AB_STAGE) && PANELAN_I2C_AB_STAGE == 4
#include <driver/gpio.h>
#include <driver/spi_master.h>
#endif

namespace {
// Rotation maps the portrait row offset onto landscape X. The integrated
// module showed a stray left-edge RAM column at offset 1; start at column 0.
PanelLanMiniSPI tft1(0);
PanelLanMiniSPI tft2(0);
PanelLanMiniSPI tft3(0);
PanelLanMiniSPI tft4(0);
PanelLanMiniSPI tft5(0);
PanelLanMiniSPI tft6(0);
PanelLanMiniSPI *const tfts[] = {&tft1, &tft2, &tft3, &tft4, &tft5, &tft6};
PanelLanMiniMCP23017 miniSelect;

#if defined(PANELAN_I2C_AB_STAGE)
#if PANELAN_I2C_AB_STAGE < 1 || PANELAN_I2C_AB_STAGE > 4
#error "PANELAN_I2C_AB_STAGE must be 1, 2, 3, or 4"
#endif
void abMark(uint8_t boundary, bool success = false, uint8_t display = 0, uint32_t detail = 0) {
    StartupDiagnostics::record(StartupDiagnostics::I2cAbBoundary, PANELAN_I2C_AB_STAGE,
                               boundary, success, detail ? detail : display);
}
#endif

#if defined(PANELAN_I2C_AB_STAGE) && PANELAN_I2C_AB_STAGE == 3
void runNoCsSpiProbe() {
    abMark(0);
    // Do not clock SPI unless the MCP has ACKed an explicit all-CS-high write.
    if (!miniSelect.diagnosticAllCsHigh()) { abMark(1); return; }
    abMark(1, true);
    lgfx::Bus_SPI bus; // Independent bus instance; no panel object or panel init.
    auto cfg = bus.config();
    cfg.spi_host = SPI2_HOST;
    cfg.spi_mode = 0;
    cfg.freq_write = 10000000;
    cfg.pin_mosi = 10;
    cfg.pin_sclk = 11;
    cfg.pin_miso = -1;
    cfg.pin_dc = 14;
    bus.config(cfg);
    const bool initialized = bus.init();
    abMark(2, initialized);
    const bool afterInit = miniSelect.diagnosticAllCsHigh();
    abMark(3, afterInit);
    bool sent = false;
    if (initialized && afterInit) {
        // Control: exercise LovyanGFX SPI host lock/config setup without clocks.
        bus.beginTransaction();
        bus.endTransaction();
    }
    const bool afterEmptyTransaction = miniSelect.diagnosticAllCsHigh();
    abMark(4, afterEmptyTransaction);
    if (initialized && afterInit && afterEmptyTransaction) {
        bus.beginTransaction();
        sent = bus.writeCommand(0x00, 8); // No CS asserted; harmless one-byte command.
        bus.wait();
        bus.endTransaction();
    }
    abMark(5, sent);
    const bool afterTransfer = miniSelect.diagnosticAllCsHigh();
    abMark(6, afterTransfer);
    bus.release();
    abMark(7, initialized);
    abMark(8, miniSelect.diagnosticAllCsHigh()); // Post-release OLATB write; recovery probe if needed.
}
#endif

#if defined(PANELAN_I2C_AB_STAGE) && PANELAN_I2C_AB_STAGE == 4
void runIdfNoCsSpiProbe() {
    abMark(0);
    if (!miniSelect.diagnosticAllCsHigh()) { abMark(1); return; }
    abMark(1, true);

    // Match the LovyanGFX DC setup without issuing any SPI clocks: init sets
    // DC high/output, command mode drives it low, and endTransaction restores it.
    const esp_err_t dcHighLevel = gpio_set_level(GPIO_NUM_14, 1);
    const esp_err_t dcDirection = gpio_set_direction(GPIO_NUM_14, GPIO_MODE_OUTPUT);
    const bool dcConfigured = dcHighLevel == ESP_OK && dcDirection == ESP_OK;
    abMark(2, dcConfigured, 0, static_cast<uint32_t>(dcDirection != ESP_OK ? dcDirection : dcHighLevel));
    const bool afterDcSetup = miniSelect.diagnosticAllCsHigh();
    abMark(3, afterDcSetup);
    const esp_err_t dcLow = gpio_set_level(GPIO_NUM_14, 0);
    abMark(4, dcLow == ESP_OK, 0, static_cast<uint32_t>(dcLow));
    const bool afterDcLow = miniSelect.diagnosticAllCsHigh();
    abMark(5, afterDcLow);
    const esp_err_t dcRestore = gpio_set_level(GPIO_NUM_14, 1);
    abMark(6, dcRestore == ESP_OK, 0, static_cast<uint32_t>(dcRestore));
    const bool afterDcRestore = miniSelect.diagnosticAllCsHigh();
    abMark(7, afterDcRestore);

    spi_bus_config_t busConfig{};
    busConfig.mosi_io_num = 10;
    busConfig.miso_io_num = -1;
    busConfig.sclk_io_num = 11;
    busConfig.quadwp_io_num = -1;
    busConfig.quadhd_io_num = -1;
    busConfig.max_transfer_sz = 1;
    busConfig.flags = SPICOMMON_BUSFLAG_MASTER;
    busConfig.intr_flags = 0;
    const esp_err_t busResult = spi_bus_initialize(SPI2_HOST, &busConfig, SPI_DMA_DISABLED);
    abMark(8, busResult == ESP_OK, 0, static_cast<uint32_t>(busResult));
    if (busResult != ESP_OK) return;

    const bool afterInit = miniSelect.diagnosticAllCsHigh();
    abMark(9, afterInit);

    spi_device_interface_config_t deviceConfig{};
    deviceConfig.clock_speed_hz = 10000000;
    deviceConfig.mode = 0;
    deviceConfig.spics_io_num = -1; // MCP retains all six physical CS lines HIGH.
    deviceConfig.queue_size = 1;
    deviceConfig.flags = SPI_DEVICE_HALFDUPLEX;
    spi_device_handle_t device = nullptr;
    const esp_err_t addResult = spi_bus_add_device(SPI2_HOST, &deviceConfig, &device);
    abMark(10, addResult == ESP_OK, 0, static_cast<uint32_t>(addResult));
    const bool afterAdd = miniSelect.diagnosticAllCsHigh();
    abMark(11, afterAdd);

    esp_err_t transferResult = ESP_ERR_INVALID_STATE;
    if (dcConfigured && afterDcSetup && afterDcLow && afterDcRestore && afterInit &&
        addResult == ESP_OK && afterAdd) {
        const esp_err_t txDc = gpio_set_level(GPIO_NUM_14, 0);
        uint8_t harmlessByte = 0x00;
        spi_transaction_t transaction{};
        transaction.length = 8;
        transaction.tx_buffer = &harmlessByte;
        if (txDc == ESP_OK) transferResult = spi_device_polling_transmit(device, &transaction);
        gpio_set_level(GPIO_NUM_14, 1);
    }
    abMark(12, transferResult == ESP_OK, 0, static_cast<uint32_t>(transferResult));
    const bool afterTransfer = miniSelect.diagnosticAllCsHigh();
    abMark(13, afterTransfer);

    esp_err_t removeResult = ESP_OK;
    if (device) removeResult = spi_bus_remove_device(device);
    const esp_err_t freeResult = spi_bus_free(SPI2_HOST);
    abMark(14, removeResult == ESP_OK && freeResult == ESP_OK,
           0, static_cast<uint32_t>(removeResult != ESP_OK ? removeResult : freeResult));
    abMark(15, miniSelect.diagnosticAllCsHigh());
}
#endif

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
// Match the main FX page accents where available; RGB565 mini TFT colors.
constexpr uint16_t gateGreen = 0x072B;
constexpr uint16_t compTeal = 0x4ED7;
constexpr uint16_t driveRed = 0xF988;
constexpr uint16_t modPurple = 0xB19F;
constexpr uint16_t delayBlue = 0x2D7F;
constexpr uint16_t reverbOrange = 0xFD20;
constexpr const char *fxNames[] = {"GATE", "COMP", "DRIVE", "MOD", "DELAY", "REVERB"};
constexpr uint16_t fxAccents[] = {gateGreen, compTeal, driveRed, modPurple, delayBlue, reverbOrange};
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
    bool pending = false;
    bool failed = false;
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
    if (card.pending) g.fillCircle(149, 11, 4, amber);
    if (card.failed) g.fillCircle(149, 11, 4, red);
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
    while (miniInitialized_ < miniCount_) {
        const uint8_t display = miniInitialized_ + 1;
        StartupDiagnostics::mark(StartupDiagnostics::tftInit(display), false);
        if (!miniSelect.select(display)) {
            // A failed selection may have left a CS asserted. Try to clear it;
            // the next select also starts with all CS high.
            miniSelect.deselect();
            miniRetryAt_ = millis() + 1000;
            StartupDiagnostics::record(StartupDiagnostics::PanelBoundary, display, StartupDiagnostics::InitRetry);
            return;
        }
        PanelLanMiniSPI &tft = *tfts[miniInitialized_];
        const uint32_t initAt = millis();
        StartupDiagnostics::record(StartupDiagnostics::PanelBoundary, display, StartupDiagnostics::InitCall);
        tft.init();
        StartupDiagnostics::record(StartupDiagnostics::PanelBoundary, display, StartupDiagnostics::InitDone, 0, millis() - initAt);
#if defined(PANELAN_I2C_AB_STAGE) && PANELAN_I2C_AB_STAGE == 2
        abMark(0, true, display);
        const bool released = miniSelect.deselect();
        abMark(1, released, display);
        if (!released) {
            miniSelect.deselect(); // Best-effort cleanup; never draw on uncertain CS.
            miniRetryAt_ = millis() + 1000;
            StartupDiagnostics::record(StartupDiagnostics::PanelBoundary, display, StartupDiagnostics::InitRetry);
            return;
        }
        const bool reselected = miniSelect.select(display);
        abMark(2, reselected, display);
        if (!reselected) {
            miniSelect.deselect(); // Best-effort all-high before the existing retry path.
            miniRetryAt_ = millis() + 1000;
            StartupDiagnostics::record(StartupDiagnostics::PanelBoundary, display, StartupDiagnostics::InitRetry);
            return;
        }
#endif
        tft.setRotation(1); // 90 degrees clockwise: 160x80 landscape
        // CS remains asserted; retain progress without serial I/O until deselect.
        StartupDiagnostics::mark(StartupDiagnostics::tftInit(display), true, false);
        StartupDiagnostics::mark(StartupDiagnostics::tftSplash(display), false, false);
        const uint32_t splashAt = millis();
        StartupDiagnostics::record(StartupDiagnostics::PanelBoundary, display, StartupDiagnostics::SplashBegin);
        drawSplash(tft); // first pixels before controller state is consulted
        const uint32_t splashDrawDone = millis();
        StartupDiagnostics::record(StartupDiagnostics::PanelBoundary, display, StartupDiagnostics::SplashDone,
                                   0, splashDrawDone - splashAt);
        if (!miniSelect.deselect()) {
            miniRetryAt_ = millis() + 1000;
            StartupDiagnostics::record(StartupDiagnostics::PanelBoundary, display, StartupDiagnostics::InitRetry);
            return;
        }
        StartupDiagnostics::mark(StartupDiagnostics::tftSplash(display), true);
        StartupDiagnostics::record(StartupDiagnostics::MiniMessage, StartupDiagnostics::PanelReady, display,
                                   static_cast<uint8_t>(tft.height()), tft.width());
        ++miniInitialized_;
    }
    splashAt_ = millis();
}

void PanelLanMiniDisplay::begin() {
    StartupDiagnostics::record(StartupDiagnostics::MiniMessage, StartupDiagnostics::SpiBegin);
    if (!miniSelect.begin()) {
        StartupDiagnostics::record(StartupDiagnostics::MiniMessage, StartupDiagnostics::McpDisabled);
        return;
    }
    miniReady_ = true;
#if defined(PANELAN_I2C_AB_STAGE) && PANELAN_I2C_AB_STAGE == 1
    abMark(0);
    const bool high = miniSelect.diagnosticAllCsHigh();
    abMark(1, high);
    if (!high) { miniReady_ = false; return; } // Do not start SPI with uncertain CS.
#elif defined(PANELAN_I2C_AB_STAGE) && PANELAN_I2C_AB_STAGE == 3
    runNoCsSpiProbe();
    return; // No TFT init, splash or live mini drawing in this variant.
#elif defined(PANELAN_I2C_AB_STAGE) && PANELAN_I2C_AB_STAGE == 4
    runIdfNoCsSpiProbe();
    return; // Raw ESP-IDF SPI test only; no TFT init, splash or live mini drawing.
#endif
    initPanels();
}

bool PanelLanMiniDisplay::readSwitches(uint8_t &pressed) {
    return miniReady_ && miniSelect.readSwitches(pressed);
}

void PanelLanMiniDisplay::update(const ControllerSnapshot &snapshot, PanelLanLVGLUI::View view, uint8_t browsedBank, uint8_t presetCount) {
#if defined(PANELAN_I2C_AB_STAGE) && (PANELAN_I2C_AB_STAGE == 3 || PANELAN_I2C_AB_STAGE == 4)
    return;
#endif
    if (!miniReady_) return;
    const uint32_t now = millis();
    if (static_cast<int32_t>(now - miniRetryAt_) < 0) return;
    if (miniInitialized_ < miniCount_) {
        initPanels();
        return;
    }
    // Keep the diagnostic visible through startup, even if the controller is
    // disconnected or BLE setup is slow. Afterwards use only SPI2 for state cards.
    if (now - splashAt_ < 5000) return;
    if (!miniUiComplete_) StartupDiagnostics::mark(StartupDiagnostics::MiniUiUpdate, false);
    // On a crowded update, service the actionable card before unrelated cards.
    uint8_t priority = miniCount_;
    if (view == PanelLanLVGLUI::View::Fx) {
        for (uint8_t j = 0; j < miniCount_; ++j)
            if (snapshot.fxSlots[j].pending) priority = j;
    } else if (view == PanelLanLVGLUI::View::Preset) {
        for (uint8_t j = 0; j < miniCount_; ++j)
            if (PhysicalPresetLayout::positions[j].role == PhysicalPresetLayout::Role::Slot &&
                snapshot.pendingHardwarePreset == browsedBank * 4 + PhysicalPresetLayout::positions[j].slot) priority = j;
    }
    for (uint8_t order = 0; order < miniCount_; ++order) {
        const uint8_t i = order == 0 && priority < miniCount_ ? priority :
            priority < miniCount_ && order <= priority ? order - 1 : order;
        const uint8_t display = i + 1;
        Card card;
        switch (view) {
        case PanelLanLVGLUI::View::Preset: {
            card.layout = Layout::Preset;
            const auto position = PhysicalPresetLayout::positions[i];
            if (position.role != PhysicalPresetLayout::Role::Slot) {
                const bool previous = position.role == PhysicalPresetLayout::Role::BankPrevious;
                const bool available = previous ? browsedBank > 0 : (browsedBank + 1) * 4 < presetCount;
                snprintf(card.name, sizeof(card.name), "BANK%c", previous ? '-' : '+');
                snprintf(card.status, sizeof(card.status), available ? "TO %u" : "END",
                         static_cast<unsigned>(browsedBank + (previous ? 0 : 2)));
                card.accent = available ? amber : muted;
                break;
            }
            const uint8_t target = static_cast<uint8_t>(browsedBank * 4 + position.slot);
            snprintf(card.name, sizeof(card.name), "P%u %s", position.slot,
                     target <= presetCount ? HardwarePresetNames::label(snapshot.hardwarePresetNames, target) : "UNAVAILABLE");
            if (target > presetCount) { card.accent = muted; strcpy(card.status, "UNAVAILABLE"); }
            else if (snapshot.pendingHardwarePreset == target) { card.accent = amber; card.mark = Mark::Dots; }
            else if (MiniActionPresentation::presetConfirmed(snapshot, target)) {
                card.accent = amber; card.filled = true; card.mark = Mark::Wave;
            } else if (snapshot.sparkStateStale || !snapshot.confirmedHardwarePreset) {
                card.accent = amber;
            } else { card.accent = muted; card.mark = Mark::Dash; }
            card.failed = MiniActionPresentation::presetFailed(snapshot, target);
            break;
        }
        case PanelLanLVGLUI::View::Fx: {
            card.layout = Layout::Gate;
            strcpy(card.name, fxNames[i]);
            const ControllerFxSlot &slot = snapshot.fxSlots[i];
            const uint16_t fxAccent = fxAccents[i];
            if (slot.pending && !snapshot.sparkStateStale && slot.known) {
                const bool enabled = MiniActionPresentation::fxEnabled(slot);
                strcpy(card.status, enabled ? "ON" : "OFF");
                card.accent = enabled ? fxAccent : muted;
                card.filled = true;
                card.pending = true;
            }
            else if (MiniActionPresentation::fxAuthoritative(snapshot, i)) {
                strcpy(card.status, slot.enabled ? "ON" : "OFF");
                card.accent = slot.enabled ? fxAccent : muted;
                card.filled = true;
            }
            else if (snapshot.sparkStateStale || !slot.known) card.accent = amber;
            card.failed = slot.actionFailed && !slot.pending;
            break;
        }
        case PanelLanLVGLUI::View::Looper:
            strcpy(card.name, "LOOPER"); strcpy(card.status, "UNASSIGNED");
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
        snprintf(key, sizeof(key), "%02u:%s|%s|%u|%u|%u|%u|%u|%u", static_cast<unsigned>(strlen(card.name)),
                  card.name, card.status, card.accent, static_cast<unsigned>(card.layout),
                  static_cast<unsigned>(card.mark), card.filled, card.pending, card.failed);
        if (!MiniActionPresentation::needsRedraw(cardDrawn_[i], view == lastView_[i], lastCard_[i], key)) continue;
        if (!miniSelect.select(display)) {
            miniSelect.deselect();
            miniRetryAt_ = millis() + 1000;
            StartupDiagnostics::record(StartupDiagnostics::PanelBoundary, display, StartupDiagnostics::DrawRetry);
            return;
        }
        const bool firstLive = !cardDrawn_[i];
        const uint32_t drawAt = millis();
        if (firstLive) StartupDiagnostics::record(StartupDiagnostics::PanelBoundary, display, StartupDiagnostics::LiveBegin);
        drawCard(*tfts[i], card);
        const uint32_t drawDone = millis();
        if (firstLive) StartupDiagnostics::record(StartupDiagnostics::PanelBoundary, display, StartupDiagnostics::LiveDone,
                                                  0, drawDone - drawAt);
        if (!miniSelect.deselect()) {
            miniRetryAt_ = millis() + 1000;
            StartupDiagnostics::record(StartupDiagnostics::PanelBoundary, display, StartupDiagnostics::DrawRetry);
            return;
        }
        lastView_[i] = view;
        strcpy(lastCard_[i], key);
        cardDrawn_[i] = true;
#ifdef PANELAN_PRESET_TRACE
        const bool actionCard = snapshot.miniActionId &&
            ((view == PanelLanLVGLUI::View::Fx && snapshot.miniActionFx && i == snapshot.miniActionTarget) ||
             (view == PanelLanLVGLUI::View::Preset && !snapshot.miniActionFx &&
              PhysicalPresetLayout::positions[i].role == PhysicalPresetLayout::Role::Slot &&
              browsedBank * 4 + PhysicalPresetLayout::positions[i].slot == snapshot.miniActionTarget));
        if (actionCard) {
            if (!snapshot.miniActionConfirmed &&
                ((snapshot.miniActionFx && snapshot.fxSlots[i].pending && !snapshot.sparkStateStale &&
                  snapshot.fxSlots[i].known) ||
                 (!snapshot.miniActionFx && snapshot.pendingHardwarePreset == snapshot.miniActionTarget)) &&
                firstDrawId_ != snapshot.miniActionId) {
                firstDrawId_ = snapshot.miniActionId;
                Serial.printf("PRESET_TRACE t=%lu event=mini_first id=%lu kind=%s target=%u input_elapsed=%lu\n",
                              static_cast<unsigned long>(millis()), static_cast<unsigned long>(snapshot.miniActionId),
                              snapshot.miniActionFx ? "fx" : "preset", snapshot.miniActionTarget,
                              static_cast<unsigned long>(millis() - snapshot.miniInputAtMs));
            } else if (snapshot.miniActionConfirmed && finalDrawId_ != snapshot.miniActionId &&
                        ((snapshot.miniActionFx &&
                          MiniActionPresentation::fxConfirmedDirection(snapshot, i, snapshot.miniActionDesiredEnabled) &&
                          card.filled) ||
                         (!snapshot.miniActionFx && !snapshot.pendingHardwarePreset &&
                          MiniActionPresentation::presetConfirmed(snapshot, snapshot.miniActionTarget) &&
                          card.filled && !card.failed && card.mark == Mark::Wave))) {
                finalDrawId_ = snapshot.miniActionId;
                Serial.printf("PRESET_TRACE t=%lu event=mini_final id=%lu kind=%s target=%u input_elapsed=%lu\n",
                              static_cast<unsigned long>(millis()), static_cast<unsigned long>(snapshot.miniActionId),
                              snapshot.miniActionFx ? "fx" : "preset", snapshot.miniActionTarget,
                              static_cast<unsigned long>(millis() - snapshot.miniInputAtMs));
            }
        }
#endif
        if (view == PanelLanLVGLUI::View::Preset)
            StartupDiagnostics::record(StartupDiagnostics::MiniMessage, StartupDiagnostics::CardSent, display,
                                       snapshot.confirmedHardwarePreset);
    }
    if (!miniUiComplete_) {
        miniUiComplete_ = true;
        StartupDiagnostics::mark(StartupDiagnostics::MiniUiUpdate, true);
    }
}
#endif
