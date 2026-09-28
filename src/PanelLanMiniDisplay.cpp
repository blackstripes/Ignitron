#include "PanelLanMiniDisplay.h"

#if defined(PANELAN_SC05X_MODE) && defined(PANELAN_LVGL_UI_MODE) && defined(PANELAN_MINI_GPIO_MODE)
#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include <soc/gpio_struct.h>
#include <soc/io_mux_reg.h>
#include <driver/gpio.h>
#include <rom/gpio.h>
#include <soc/gpio_sig_map.h>
#include "PanelLanMiniReference.h"
#include "controller/HardwarePresetNames.h"

namespace {
// No SPI peripheral, DMA, pin autodetection or MISO. Mode 0, MSB first.
struct ReferenceWire {
    uint32_t bytes = 0;
    uint32_t hash = 2166136261u;
    void byte(uint8_t value) {
        ++bytes;
        hash = (hash ^ value) * 16777619u;
        for (uint8_t mask = 0x80; mask; mask >>= 1) {
            if (value & mask) lgfx::gpio_hi(10); else lgfx::gpio_lo(10);
            lgfx::gpio_hi(11);
            delayMicroseconds(1);
            lgfx::gpio_lo(11);
        }
    }
    void command(uint8_t value) { lgfx::gpio_lo(14); byte(value); }
    void data(uint8_t value) { lgfx::gpio_hi(14); byte(value); }
    void pause(unsigned ms) { delay(ms); }
};

void selectGPIO() {
    for (int pin = 10; pin <= 14; ++pin) {
        gpio_set_level(static_cast<gpio_num_t>(pin), pin >= 12);
        PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[pin], PIN_FUNC_GPIO);
        gpio_matrix_out(pin, SIG_GPIO_OUT_IDX, false, false);
        gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_OUTPUT);
    }
    lgfx::gpio_lo(11);
}

// 5x7 column glyphs, digits and uppercase ASCII; unknown characters are blank.
constexpr uint8_t glyphs[][5] = {
    {0x3E,0x51,0x49,0x45,0x3E}, {0,0x42,0x7F,0x40,0},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E},
    {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36},
    {0x3E,0x41,0x41,0x41,0x22}, {0x7F,0x41,0x41,0x22,0x1C},
    {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F},
    {0,0x41,0x7F,0x41,0}, {0x20,0x40,0x41,0x3F,0x01},
    {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F},
    {0x3E,0x41,0x41,0x41,0x3E}, {0x7F,0x09,0x09,0x09,0x06},
    {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01},
    {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F},
    {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}
};

bool textPixel(const char *text, int x, int y, int top) {
    if (y < top || y >= top + 7 || x < 5) return false;
    const int index = (x - 5) / 6;
    if ((x - 5) % 6 == 5 || !text[index]) return false;
    const char raw = text[index];
    const char c = raw >= 'a' && raw <= 'z' ? raw - 'a' + 'A' : raw;
    const int glyph = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'Z' ? c - 'A' + 10 : -1;
    return glyph >= 0 && (glyphs[glyph][(x - 5) % 6] & (1 << (y - top)));
}

void window(ReferenceWire &wire, int y, int count) {
    wire.command(0x2A);
    wire.data(0); wire.data(26); wire.data(0); wire.data(105);
    wire.command(0x2B);
    wire.data(0); wire.data(y + 1); wire.data(0); wire.data(y + count);
    wire.command(0x2C);
}
}

#ifdef PANELAN_MINI_COEXISTENCE_TRACE
void PanelLanMiniDisplay::traceHardware(const char *checkpoint, bool force) {
    struct State {
        uint32_t enable;
        uint32_t controlLevels;
        uint32_t mux[5];
        uint32_t route[5];
        uint32_t pin[5];
    };
    static State previous{};
    static bool captured = false;
    State current{};
    current.enable = GPIO.enable & (0x1Fu << 10);
    // MOSI/SCK are peripheral outputs; their GPIO output latches are not levels.
    current.controlLevels = GPIO.out & (0x7u << 12);
    for (int i = 0; i < 5; ++i) {
        const int pin = 10 + i;
        current.mux[i] = REG_READ(GPIO_PIN_MUX_REG[pin]);
        current.route[i] = GPIO.func_out_sel_cfg[pin].val;
        current.pin[i] = GPIO.pin[pin].val;
    }
    if (!force && captured && !memcmp(&previous, &current, sizeof(current))) return;
    Serial.printf("MINI TRACE %lu ms: %s enable=%08lx control-latches=%08lx\n",
                  static_cast<unsigned long>(millis()), checkpoint,
                  static_cast<unsigned long>(current.enable),
                  static_cast<unsigned long>(current.controlLevels));
    for (int i = 0; i < 5; ++i) {
        Serial.printf("  IO%d mux=%08lx route=%08lx pin=%08lx\n", 10 + i,
                      static_cast<unsigned long>(current.mux[i]),
                      static_cast<unsigned long>(current.route[i]),
                      static_cast<unsigned long>(current.pin[i]));
    }
    previous = current;
    captured = true;
}

void PanelLanMiniDisplay::referenceFrame(bool reset) {
    holdMini_ = true;
    sending_ = false;
    drawn_ = false;
    diagnosticTransport_ = "gpio-mode0";
    selectGPIO();
    ReferenceWire wire;
    if (reset) {
        lgfx::gpio_lo(12);
        delay(20);
        lgfx::gpio_hi(12);
        delay(150);
    }
    lgfx::gpio_lo(13);
    if (reset) {
        // Replay the exact installed ST7735S command tables, but NOT LGFX's
        // bus, init lifecycle, graphics state, or address-window machinery.
        for (uint8_t n = 0; const uint8_t *list = panel_.getInitCommands(n); ++n)
            PanelLanMiniReference::replayInitList(wire, list);
        wire.command(0x20); // inversion off, matching the normal profile
    }
    PanelLanMiniReference::frame(wire);
    lgfx::gpio_hi(13);
    lgfx::gpio_hi(14);
    rawBytes_ = wire.bytes;
    rawHash_ = wire.hash;
    diagnosticMode_ = reset ? "raw-reset-bars" : "raw-no-reset-bars";
    ++diagnosticSequence_;
}

void PanelLanMiniDisplay::diagnosticStatus() const {
    Serial.printf("{\"mini\":\"status\",\"seq\":%lu,\"mode\":\"%s\",\"transport\":\"%s\","
                  "\"controller\":%s,\"main_only\":%s,\"hold_mini\":%s,"
                  "\"last_raw_bytes\":%lu,\"last_raw_fnv1a\":%lu,\"readback\":false}\n",
                  static_cast<unsigned long>(diagnosticSequence_), diagnosticMode_, diagnosticTransport_,
                  runController_ ? "true" : "false", serviceMain_ ? "true" : "false",
                  holdMini_ ? "true" : "false", static_cast<unsigned long>(rawBytes_),
                  static_cast<unsigned long>(rawHash_));
}

void PanelLanMiniDisplay::diagnosticCommand(const String &args) {
    if (args == "status") {
        diagnosticStatus();
        traceHardware("requested status", true);
        return;
    }
    Serial.printf("{\"mini\":\"begin\",\"command\":\"%s\"}\n", args.c_str());
    traceHardware("before mini command", true);
    if (args == "pause") {
        runController_ = false;
        serviceMain_ = false;
    } else if (args == "main") {
        runController_ = false;
        serviceMain_ = true;
    } else if (args == "run") {
        runController_ = true;
        serviceMain_ = false;
        holdMini_ = true;
    } else if (args == "raw-frame" || args == "raw-reset") {
        referenceFrame(args == "raw-reset");
    } else if (args == "dynamic") {
        holdMini_ = false;
        drawn_ = false;
        runController_ = true;
        serviceMain_ = false;
        diagnosticMode_ = "dynamic";
    } else {
        Serial.println("mini status|pause|main|run|raw-frame|raw-reset|dynamic");
        return;
    }
    diagnosticStatus();
    traceHardware("after mini command");
}
#endif

PanelLanMiniDisplay::PanelLanMiniDisplay() = default;

void PanelLanMiniDisplay::begin() {
    // BLK is tied to external 3.3 V, never a GPIO output.
    Serial.println("ST7735S: init start");
    selectGPIO();
    lgfx::gpio_lo(12);
    delay(20);
    lgfx::gpio_hi(12);
    delay(150);
    lgfx::gpio_lo(13);
    ReferenceWire wire;
    for (uint8_t n = 0; const uint8_t *list = panel_.getInitCommands(n); ++n)
        PanelLanMiniReference::replayInitList(wire, list);
    wire.command(0x20); // inversion off
    wire.command(0x3A); wire.data(0x55); // RGB565
    wire.command(0x36); wire.data(0x08); // rotation 0, BGR
    lgfx::gpio_hi(13);
    lgfx::gpio_hi(14);
    drawn_ = false;
    Serial.println("ST7735S: GPIO init complete, portrait 80 x 160");
}

void PanelLanMiniDisplay::update(const ControllerSnapshot &snapshot, PanelLanLVGLUI::View view) {
#ifdef PANELAN_MINI_COEXISTENCE_TRACE
    if (holdMini_) return;
#endif
    const char *title = HardwarePresetNames::label(snapshot.hardwarePresetNames, 1);
    const char *status = "SYNC";
    const char *footer = "PRESET PAGE";
    // RGB565 accents; trust/pending/error are conveyed in text as well.
    uint16_t accent = 0xFD20; // amber
    switch (view) {
    case PanelLanLVGLUI::View::Preset:
        if (snapshot.pendingHardwarePreset != 0) {
            status = snapshot.pendingHardwarePreset == 1 ? "SELECTING" : "CHANGING";
        } else if (snapshot.presetActionFailed) {
            status = "RETRY";
            accent = 0xF800;
        } else if (!snapshot.sparkStateStale && snapshot.confirmedHardwarePreset != 0) {
            status = snapshot.confirmedHardwarePreset == 1 ? "CURRENT" : "NOT CURRENT";
            accent = snapshot.confirmedHardwarePreset == 1 ? 0x07E0 : 0x9CF3;
        }
        break;
    case PanelLanLVGLUI::View::Fx: {
        title = "GATE";
        footer = "FX PAGE";
        const ControllerFxSlot &slot = snapshot.fxSlots[0];
        if (slot.pending) {
            status = slot.pendingDesiredEnabled ? "TURNING ON" : "TURNING OFF";
        } else if (slot.actionFailed) {
            status = "RETRY";
            accent = 0xF800;
        } else if (!snapshot.sparkStateStale && slot.known) {
            status = slot.enabled ? "ON" : "OFF";
            accent = slot.enabled ? 0x07E0 : 0x9CF3;
        }
        break;
    }
    case PanelLanLVGLUI::View::Looper:
        title = "LOOPER";
        status = "NO SWITCH";
        footer = "UNAVAILABLE";
        break;
    case PanelLanLVGLUI::View::Tuner:
        title = "TUNER";
        status = !snapshot.tunerActive ? "INACTIVE" : snapshot.tunerSampleFresh ? "FRESH" : "STALE";
        footer = "TUNER PAGE";
        accent = snapshot.tunerActive && snapshot.tunerSampleFresh ? 0x07E0 : 0xFD20;
        break;
    case PanelLanLVGLUI::View::Device:
        title = "DEVICE";
        footer = "LINK STATUS";
        switch (snapshot.connectionPhase) {
        case ControllerConnectionPhase::Scanning: status = "SEARCHING"; break;
        case ControllerConnectionPhase::Reconnecting: status = "RECONNECT"; break;
        case ControllerConnectionPhase::Identifying:
        case ControllerConnectionPhase::Syncing: status = "SYNCING"; break;
        case ControllerConnectionPhase::Ready: status = "CONNECTED"; accent = 0x07E0; break;
        }
        break;
    }
    // This is a write-only display. A one-shot cached draw cannot recover if
    // the first frame is lost during combined board/controller startup.
    const uint32_t now = millis();
    if (!sending_) {
        if (drawn_ && now - lastDrawAt_ < 2000 && !strcmp(lastTitle_, title) &&
            !strcmp(lastStatus_, status) && !strcmp(lastFooter_, footer) &&
            lastAccent_ == accent) return;
        snprintf(title_, sizeof(title_), "%s", title);
        snprintf(status_, sizeof(status_), "%s", status);
        snprintf(footer_, sizeof(footer_), "%s", footer);
        accent_ = accent;
        row_ = 0;
        sending_ = true;
    }
    // Four visible rows per loop iteration (~640 RGB565 bytes). Keep BLE and
    // LVGL serviced between chunks; never block for the entire frame.
    const int count = row_ + 4 <= 160 ? 4 : 160 - row_;
    ReferenceWire wire;
    lgfx::gpio_lo(13);
    window(wire, row_, count);
    for (int y = row_; y < row_ + count; ++y) {
        for (int x = 0; x < 80; ++x) {
            uint16_t color = y < 8 || x == 0 || x == 79 || y == 159 ? accent_ : 0;
            if (textPixel(title_, x, y, 24) || textPixel(footer_, x, y, 140)) color = 0xFFFF;
            if (textPixel(status_, x, y, 52)) color = accent_;
            wire.data(color >> 8);
            wire.data(color);
        }
    }
    lgfx::gpio_hi(13);
    lgfx::gpio_hi(14);
    row_ += count;
    if (row_ == 160) {
        sending_ = false;
        if (!drawn_) Serial.printf("ST7735S: first frame sent (no readback) title=%s status=%s footer=%s\n", title_, status_, footer_);
        snprintf(lastTitle_, sizeof(lastTitle_), "%s", title_);
        snprintf(lastStatus_, sizeof(lastStatus_), "%s", status_);
        snprintf(lastFooter_, sizeof(lastFooter_), "%s", footer_);
        lastAccent_ = accent_;
        lastDrawAt_ = millis();
        drawn_ = true;
    }
}
#endif
