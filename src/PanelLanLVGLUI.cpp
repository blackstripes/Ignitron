#include "PanelLanLVGLUI.h"
#include "controller/ControllerActions.h"

#include <esp_heap_caps.h>
#include <cmath>

#if defined(PANELAN_SC05X_MODE) && defined(PANELAN_LVGL_UI_MODE)

namespace {
PanelLanLVGLUI *uiInstance = nullptr;

// Looper colors communicate transport and intent; text always carries the
// same meaning so recording, pending and unavailable remain unambiguous.
namespace LooperTheme {
constexpr uint32_t surface = 0x10191F;
constexpr uint32_t border = 0x31434F;
constexpr uint32_t text = 0xF1F4F7;
constexpr uint32_t secondary = 0xA9BAC5;
constexpr uint32_t info = 0x4ED6F0;
constexpr uint32_t muted = 0x657783;
constexpr uint32_t record = 0xFF6876;
constexpr uint32_t recordFill = 0x56252F;
constexpr uint32_t play = 0x55E6A0;
constexpr uint32_t playFill = 0x174534;
constexpr uint32_t warning = 0xFFD06A;
constexpr uint32_t dangerFill = 0x962E3C;
}

void styleLooperButton(lv_obj_t *button, uint32_t fill, uint32_t accent) {
    lv_obj_set_style_bg_color(button, lv_color_hex(fill), 0);
    lv_obj_set_style_bg_grad_color(button, lv_color_hex(0x090E12), 0);
    lv_obj_set_style_border_color(button, lv_color_hex(accent), 0);
    lv_obj_set_style_text_color(button, lv_color_hex(LooperTheme::text), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(button, 1), lv_color_hex(accent), 0);
}

void drawLooperSurface(lv_event_t *event) {
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t bounds;
    lv_obj_get_coords(lv_event_get_target_obj(event), &bounds);
    auto line = [&](int x1, int y1, int x2, int y2) {
        lv_draw_line_dsc_t dsc;
        lv_draw_line_dsc_init(&dsc);
        dsc.p1 = {bounds.x1 + x1, bounds.y1 + y1};
        dsc.p2 = {bounds.x1 + x2, bounds.y1 + y2};
        dsc.color = lv_color_hex(LooperTheme::border);
        dsc.width = 1;
        lv_draw_line(layer, &dsc);
    };
    line(9, 25, 298, 25);
    for (int x : {77, 155, 215}) line(x, 7, x, 20);
    // Spark does not currently report a trustworthy playhead. Preserve the
    // concept's segmented rail, but never animate or fill invented progress.
    for (int segment = 0; segment < 30; ++segment) {
        lv_draw_rect_dsc_t dsc;
        lv_draw_rect_dsc_init(&dsc);
        dsc.bg_color = lv_color_hex(segment % 10 == 0 ? 0x465866 : 0x293842);
        lv_area_t area = {bounds.x1 + 10 + segment * 6, bounds.y1 + 131,
                          bounds.x1 + 13 + segment * 6, bounds.y1 + 140};
        lv_draw_rect(layer, &dsc, &area);
    }
}

// A subdued amplifier cabinet gives the preset card the concept's physical
// texture. Draw directly into LVGL's current partial buffer; no full-screen
// image allocation, extra framebuffer, or fake amp/model metadata is needed.
void drawPresetCabinet(lv_event_t *event) {
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t bounds;
    lv_obj_get_coords(lv_event_get_target_obj(event), &bounds);
    auto rect = [&](int x, int y, int w, int h, uint32_t fill, uint32_t border, int radius) {
        lv_draw_rect_dsc_t dsc;
        lv_draw_rect_dsc_init(&dsc);
        dsc.bg_color = lv_color_hex(fill);
        dsc.border_color = lv_color_hex(border);
        dsc.border_width = 1;
        dsc.radius = radius;
        lv_area_t area = {bounds.x1 + x, bounds.y1 + y,
                          bounds.x1 + x + w - 1, bounds.y1 + y + h - 1};
        lv_draw_rect(layer, &dsc, &area);
    };
    auto line = [&](int x1, int y1, int x2, int y2, uint32_t color) {
        lv_draw_line_dsc_t dsc;
        lv_draw_line_dsc_init(&dsc);
        dsc.p1 = {bounds.x1 + x1, bounds.y1 + y1};
        dsc.p2 = {bounds.x1 + x2, bounds.y1 + y2};
        dsc.color = lv_color_hex(color);
        dsc.width = 1;
        lv_draw_line(layer, &dsc);
    };
    rect(3, 3, 300, 151, 0x111619, 0x354047, 7);
    rect(8, 8, 290, 27, 0x11191C, 0x283237, 4);
    line(16, 10, 272, 10, 0x655035);
    rect(27, 15, 53, 12, 0x342024, 0x452A2B, 2);
    for (int x = 168; x < 280; x += 23) {
        rect(x, 17, 7, 7, 0x404447, 0x171C1F, 3);
        line(x + 3, 17, x + 3, 19, 0x887D61);
    }
    rect(9, 38, 288, 110, 0x191D1D, 0x2E3537, 4);
    // Woven grille, deliberately low contrast behind the foreground type.
    for (int x = 13; x < 294; x += 5)
        line(x, 42, x, 144, 0x292B27);
    for (int y = 43; y < 145; y += 4)
        line(13, y, 293, y, 0x202422);
    lv_draw_rect_dsc_t shade;
    lv_draw_rect_dsc_init(&shade);
    shade.bg_color = lv_color_hex(0x03090D);
    shade.bg_opa = LV_OPA_60;
    shade.radius = 6;
    lv_area_t overlay = {bounds.x1 + 4, bounds.y1 + 4, bounds.x1 + 302, bounds.y1 + 152};
    lv_draw_rect(layer, &shade, &overlay);
}

// Small vector glyphs keep the concept's visual vocabulary crisp at 320x240,
// without a bitmap framebuffer or a collection of decorative LVGL objects.
void drawGlyph(lv_event_t *event) {
    lv_obj_t *obj = lv_event_get_target_obj(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t bounds;
    lv_obj_get_coords(obj, &bounds);
    const uint8_t glyph = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
    const lv_color_t color = lv_obj_get_style_text_color(obj, LV_PART_MAIN);
    const bool compact = glyph >= 11;
    auto scaled = [&](int value) { return compact ? (value + 1) / 2 : value; };
    auto line = [&](int x1, int y1, int x2, int y2, int width = 2) {
        lv_draw_line_dsc_t dsc;
        lv_draw_line_dsc_init(&dsc);
        dsc.p1 = {bounds.x1 + scaled(x1), bounds.y1 + scaled(y1)};
        dsc.p2 = {bounds.x1 + scaled(x2), bounds.y1 + scaled(y2)};
        dsc.width = compact ? 1 : width;
        dsc.color = color;
        dsc.round_start = dsc.round_end = 1;
        lv_draw_line(layer, &dsc);
    };
    auto arc = [&](int x, int y, int radius, int start, int end, int width = 2) {
        lv_draw_arc_dsc_t dsc;
        lv_draw_arc_dsc_init(&dsc);
        dsc.center = {bounds.x1 + scaled(x), bounds.y1 + scaled(y)};
        dsc.radius = scaled(radius);
        dsc.start_angle = start;
        dsc.end_angle = end;
        dsc.width = compact ? 1 : width;
        dsc.color = color;
        dsc.rounded = 1;
        lv_draw_arc(layer, &dsc);
    };
    switch (compact ? glyph - 11 : glyph) {
    case 0: { // Gate: waveform.
        static const int8_t points[][2] = {{2,18},{7,18},{10,9},{14,29},{18,3},
                                          {22,32},{26,7},{30,25},{33,18},{37,18}};
        for (uint8_t i = 1; i < 10; ++i)
            line(points[i-1][0], points[i-1][1], points[i][0], points[i][1]);
        break;
    }
    case 1: // Compressor: inward compression arrows, matching the crossed concept mark.
        line(10,5,16,15,3); line(10,29,28,5,3);
        line(22,21,28,29,3); line(10,17,16,15); line(16,15,16,9);
        line(22,27,22,21); line(22,21,28,21);
        break;
    case 2: // Drive: vacuum tube.
        arc(19,12,9,180,360,3);
        line(11,12,11,29,3); line(27,12,27,29,3); line(11,29,27,29,3);
        line(15,32,23,32,2); line(16,32,16,35); line(22,32,22,35);
        line(15,24,15,17); line(15,17,19,20); line(19,20,23,17); line(23,17,23,24);
        break;
    case 3: { // Modulation: a smooth, sampled sine wave.
        static const int8_t points[][2] = {{2,20},{4,12},{6,7},{8,6},{10,8},{12,14},
            {14,23},{16,29},{18,30},{20,27},{22,20},{24,14},{26,13},{28,16},{30,24},
            {32,29},{34,28},{36,23},{38,16}};
        for (uint8_t i = 1; i < 19; ++i)
            line(points[i-1][0], points[i-1][1], points[i][0], points[i][1]);
        break;
    }
    case 4: // Delay: a note and receding echoes.
        arc(21,28,5,0,360,3); line(25,27,25,4,3);
        line(25,4,31,13,3); line(31,13,31,17);
        line(4,25,5,25); line(9,23,11,23); line(14,21,17,21);
        break;
    case 5: // Reverb: concentric reflections.
        arc(19,18,16,0,360); arc(19,18,11,0,360); arc(19,18,6,0,360);
        break;
    case 6: // Home.
        line(2,9,10,2,1); line(10,2,18,9,1);
        line(4,8,4,18,1); line(4,18,8,18,1); line(8,18,8,12,1);
        line(8,12,12,12,1); line(12,12,12,18,1); line(12,18,16,18,1); line(16,18,16,8,1);
        break;
    case 7: // FX sliders.
        line(4,2,4,18,1); line(10,2,10,18,1); line(16,2,16,18,1);
        line(2,7,6,7,3); line(8,13,12,13,3); line(14,6,18,6,3);
        break;
    case 8: // Looper.
        arc(5,10,5,45,315,1); arc(15,10,5,225,495,1);
        line(8,7,12,13,1); line(8,13,12,7,1);
        break;
    case 9: // Tuning fork.
        line(5,2,5,10,1); line(15,2,15,10,1);
        arc(10,10,6,0,180,1); line(10,15,10,19,1);
        break;
    case 10: // Device / settings.
        arc(10,10,6,0,360,1); arc(10,10,3,0,360,1);
        line(10,1,10,4,1); line(10,16,10,19,1); line(1,10,4,10,1); line(16,10,19,10,1);
        line(4,4,6,6,1); line(14,14,16,16,1); line(4,16,6,14,1); line(14,6,16,4,1);
        break;
    }
}

lv_obj_t *createGlyph(lv_obj_t *parent, uint8_t glyph, int width, int height) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, width, height);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(obj, drawGlyph, LV_EVENT_DRAW_MAIN,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(glyph)));
    return obj;
}

lv_obj_t *createPanel(lv_obj_t *parent, int x, int y, int width, int height) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x182229), 0);
    lv_obj_set_style_bg_grad_color(obj, lv_color_hex(0x080F13), 0);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x34434D), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, 6, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t *createText(lv_obj_t *parent, const char *text, int x, int y, int width,
                     const lv_font_t *font, uint32_t color, bool centered = false) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, centered ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT, 0);
    return label;
}

// Spark's established 0..1 tuner value maps to -50..+50 cents (the same
// conversion used by the legacy display and LEDs). Only fresh observations
// get a pointer; out-of-range values remain visible in the numeric readout.
void drawTunerMeter(lv_event_t *event) {
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t bounds;
    lv_obj_get_coords(lv_event_get_target_obj(event), &bounds);
    const auto &snapshot = *static_cast<const ControllerSnapshot *>(lv_event_get_user_data(event));
    auto line = [&](int x, int top, int bottom, uint32_t color, int width = 1) {
        lv_draw_line_dsc_t dsc;
        lv_draw_line_dsc_init(&dsc);
        dsc.p1 = {bounds.x1 + x, bounds.y1 + top};
        dsc.p2 = {bounds.x1 + x, bounds.y1 + bottom};
        dsc.color = lv_color_hex(color);
        dsc.width = width;
        dsc.round_start = dsc.round_end = 1;
        lv_draw_line(layer, &dsc);
    };
    for (int i = 0; i <= 20; ++i) {
        const int height = i == 10 ? 22 : i % 5 == 0 ? 17 : 10;
        line(11 + i * 13, 25 - height, 25, i == 10 ? 0xA7B6BF : 0x53636D);
    }
    if (snapshot.tunerActive && snapshot.tunerSampleFresh &&
        !snapshot.tunerNote.empty() && snapshot.tunerNote != " " &&
        std::isfinite(snapshot.tunerOffset) && snapshot.tunerOffset >= 0.0f &&
        snapshot.tunerOffset <= 1.0f) {
        const int x = 11 + static_cast<int>(snapshot.tunerOffset * 260.0f + 0.5f);
        const bool inTune = snapshot.tunerOffsetCents >= -5 && snapshot.tunerOffsetCents <= 5;
        line(x, 1, 27, inTune ? 0x173D2A : 0x123846, 11);
        line(x, 1, 27, inTune ? 0x00E65D : 0x4ED6F0, 5);
    }
}

// Decorative empty states, not controls or simulated amp readings. These are
// drawn in the existing partial buffer, with no image/canvas allocation.
void drawInstrumentEmptyState(lv_event_t *event) {
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t bounds;
    lv_obj_get_coords(lv_event_get_target_obj(event), &bounds);
    const bool tuner = lv_event_get_user_data(event) != nullptr;
    auto line = [&](int x1, int y1, int x2, int y2, uint32_t color, int width = 1) {
        lv_draw_line_dsc_t dsc;
        lv_draw_line_dsc_init(&dsc);
        dsc.p1 = {bounds.x1 + x1, bounds.y1 + y1};
        dsc.p2 = {bounds.x1 + x2, bounds.y1 + y2};
        dsc.color = lv_color_hex(color);
        dsc.width = width;
        dsc.round_start = dsc.round_end = 1;
        lv_draw_line(layer, &dsc);
    };
    auto arc = [&](int x, int y, int radius, int start, int end, uint32_t color, int width) {
        lv_draw_arc_dsc_t dsc;
        lv_draw_arc_dsc_init(&dsc);
        dsc.center = {bounds.x1 + x, bounds.y1 + y};
        dsc.radius = radius;
        dsc.start_angle = start;
        dsc.end_angle = end;
        dsc.color = lv_color_hex(color);
        dsc.width = width;
        dsc.rounded = 1;
        lv_draw_arc(layer, &dsc);
    };
    if (tuner) {
        // No centered green pointer: that would falsely imply an in-tune note.
        line(121, 51, 141, 51, 0x8C9CA9, 5);
        line(165, 51, 185, 51, 0x8C9CA9, 5);
        line(27, 108, 279, 108, 0x26333D);
        for (int i = 0; i <= 12; ++i) {
            const int x = 27 + i * 21;
            const int height = i == 6 ? 22 : i % 3 == 0 ? 16 : 10;
            line(x, 107 - height, x, 107, i == 6 ? 0x637581 : 0x485862);
        }
    } else {
        // A large, subdued infinity mark echoes the concept's circular
        // transport area without presenting unsupported transport buttons.
        arc(122, 59, 26, 45, 315, 0x627782, 3);
        arc(184, 59, 26, 225, 495, 0x627782, 3);
        line(140, 41, 166, 77, 0x627782, 3);
        line(140, 77, 166, 41, 0x627782, 3);
        for (int x = 14; x < 292; x += 7)
            line(x, 141, x, 151, 0x25333C, 3);
    }
}

void drawDevicePortrait(lv_event_t *event) {
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t bounds;
    lv_obj_get_coords(lv_event_get_target_obj(event), &bounds);
    const bool headphones = lv_obj_get_user_data(lv_event_get_target_obj(event)) != nullptr;
    auto rect = [&](int x, int y, int width, int height, uint32_t color, int radius) {
        lv_draw_rect_dsc_t dsc;
        lv_draw_rect_dsc_init(&dsc);
        dsc.bg_color = lv_color_hex(color);
        dsc.border_color = lv_color_hex(0x697782);
        dsc.border_width = 1;
        dsc.radius = radius;
        lv_area_t area = {bounds.x1 + x, bounds.y1 + y,
                          bounds.x1 + x + width - 1, bounds.y1 + y + height - 1};
        lv_draw_rect(layer, &dsc, &area);
    };
    if (headphones) {
        lv_draw_arc_dsc_t arc;
        lv_draw_arc_dsc_init(&arc);
        arc.center = {bounds.x1 + 29, bounds.y1 + 30};
        arc.radius = 20;
        arc.start_angle = 180;
        arc.end_angle = 360;
        arc.width = 5;
        arc.color = lv_color_hex(0xA8B6BE);
        lv_draw_arc(layer, &arc);
        rect(7, 28, 11, 22, 0x253540, 4);
        rect(41, 28, 11, 22, 0x253540, 4);
    } else {
        rect(18, 9, 22, 6, 0x131D23, 2);
        rect(4, 14, 51, 39, 0x1F292F, 4);
        rect(8, 18, 43, 8, 0x3B332A, 2);
        rect(8, 29, 43, 20, 0x303A3E, 1);
    }
}
}

void PanelLanLVGLUI::flushDisplay(lv_display_t *display, const lv_area_t *area, uint8_t *pixelMap) {
    const uint32_t width = area->x2 - area->x1 + 1;
    const uint32_t height = area->y2 - area->y1 + 1;
    uiInstance->tft_.startWrite();
    uiInstance->tft_.setAddrWindow(area->x1, area->y1, width, height);
    uiInstance->tft_.writePixels(reinterpret_cast<uint16_t *>(pixelMap), width * height, true);
    uiInstance->tft_.endWrite();
    lv_display_flush_ready(display);
}

void PanelLanLVGLUI::readTouch(lv_indev_t *, lv_indev_data_t *data) {
    if (uiInstance->injectedTouchCount_ != 0) {
        const InjectedTouch &touch = uiInstance->injectedTouches_[uiInstance->injectedTouchHead_];
        data->point.x = touch.x;
        data->point.y = touch.y;
        if (uiInstance->injectedTouchPressed_) {
            uiInstance->injectedTouchPressed_ = false;
            uiInstance->injectedTouchHead_ =
                (uiInstance->injectedTouchHead_ + 1) % kInjectedTouchQueueSize;
            --uiInstance->injectedTouchCount_;
            data->state = LV_INDEV_STATE_RELEASED;
        } else {
            uiInstance->injectedTouchPressed_ = true;
            data->state = LV_INDEV_STATE_PRESSED;
        }
        return;
    }
    uint16_t x = 0;
    uint16_t y = 0;
    if (uiInstance->tft_.getTouch(&x, &y)) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = x;
        data->point.y = y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void PanelLanLVGLUI::injectTouch(uint16_t x, uint16_t y) {
    if (x >= kDisplayWidth || y >= kDisplayHeight) {
        return;
    }
    if (injectedTouchCount_ == kInjectedTouchQueueSize) {
        return;
    }
    const uint8_t tail = (injectedTouchHead_ + injectedTouchCount_) % kInjectedTouchQueueSize;
    injectedTouches_[tail] = {x, y};
    ++injectedTouchCount_;
}

void PanelLanLVGLUI::begin() {
    uiInstance = this;
    tft_.begin();
    tft_.setRotation(1);
    lv_init();
    lastLvglTickAt_ = millis();

    display_ = lv_display_create(kDisplayWidth, kDisplayHeight);
    lv_display_set_color_format(display_, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display_, drawBuffer_, nullptr, sizeof(drawBuffer_), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display_, flushDisplay);

    lv_indev_t *touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, readTouch);
    lv_indev_set_display(touch, display_);
    createUi();
    renderStatus(ControllerSnapshot{});
}

void PanelLanLVGLUI::createUi() {
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x080D10), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    lv_obj_t *header = lv_obj_create(screen);
    lv_obj_set_size(header, 320, 30);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x111B21), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);

    lv_obj_t *title = lv_label_create(header);
    headerTitle_ = title;
    lv_label_set_text(title, "IGNITRON");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 10, 0);

    connectionLabel_ = lv_label_create(header);
    lv_obj_set_width(connectionLabel_, 140);
    lv_obj_set_style_text_align(connectionLabel_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(connectionLabel_, LV_ALIGN_RIGHT_MID, -10, 0);

    lv_obj_t *hero = lv_button_create(screen);
    lv_obj_set_size(hero, 308, 158);
    lv_obj_align(hero, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_set_style_bg_color(hero, lv_color_hex(0x11181D), 0);
    lv_obj_set_style_border_color(hero, lv_color_hex(0x34434C), 0);
    lv_obj_set_style_border_width(hero, 1, 0);
    lv_obj_set_style_radius(hero, 7, 0);
    lv_obj_set_style_pad_all(hero, 0, 0);
    lv_obj_set_style_shadow_width(hero, 0, 0);
    lv_obj_remove_flag(hero, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(hero, drawPresetCabinet, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(hero, onPresetCardClicked, LV_EVENT_CLICKED, nullptr);

    identityLabel_ = lv_label_create(hero);
    lv_obj_set_width(identityLabel_, 270);
    lv_obj_set_style_text_font(identityLabel_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(identityLabel_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(identityLabel_, lv_color_hex(0xD7DFE3), 0);
    lv_obj_align(identityLabel_, LV_ALIGN_TOP_MID, 0, 21);

    presetNameLabel_ = lv_label_create(hero);
    lv_obj_set_width(presetNameLabel_, 282);
    lv_label_set_long_mode(presetNameLabel_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(presetNameLabel_, lv_color_white(), 0);
    lv_obj_set_style_text_font(presetNameLabel_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(presetNameLabel_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(presetNameLabel_, LV_ALIGN_TOP_MID, 0, 42);

    presetDescriptionLabel_ = lv_label_create(hero);
    lv_obj_set_width(presetDescriptionLabel_, 274);
    lv_label_set_long_mode(presetDescriptionLabel_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(presetDescriptionLabel_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(presetDescriptionLabel_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(presetDescriptionLabel_, lv_color_hex(0xABB8C2), 0);
    lv_obj_align(presetDescriptionLabel_, LV_ALIGN_TOP_MID, 0, 77);

    presetMetaLabel_ = lv_label_create(hero);
    lv_label_set_text(presetMetaLabel_, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(presetMetaLabel_, lv_color_hex(0x92A2AD), 0);
    lv_obj_align(presetMetaLabel_, LV_ALIGN_TOP_RIGHT, -10, 20);

    const int16_t fxX[] = {-127, -76, -25, 26, 77, 128};
    for (uint8_t fx = 0; fx < 6; ++fx) {
        lv_obj_t *tile = lv_obj_create(screen);
        fxTiles_[fx] = tile;
        lv_obj_set_size(tile, 47, 47);
        lv_obj_align(tile, LV_ALIGN_TOP_MID, fxX[fx], 143);
        lv_obj_set_style_bg_color(tile, lv_color_hex(0x151F25), 0);
        lv_obj_set_style_border_color(tile, lv_color_hex(0x34454F), 0);
        lv_obj_set_style_border_width(tile, 1, 0);
        lv_obj_set_style_radius(tile, 5, 0);
        lv_obj_set_style_pad_all(tile, 0, 0);
        lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *label = lv_label_create(tile);
        lv_obj_set_width(label, 45);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -5);
        fxIcons_[fx] = createGlyph(tile, fx + 11, 20, 19);
        lv_obj_align(fxIcons_[fx], LV_ALIGN_TOP_MID, 0, 5);
        fxStateLabels_[fx] = lv_label_create(tile);
        lv_label_set_text(fxStateLabels_[fx], "?");
        lv_obj_set_style_text_font(fxStateLabels_[fx], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_align(fxStateLabels_[fx], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(fxStateLabels_[fx], LV_ALIGN_TOP_RIGHT, -3, 2);
    }

    presetPicker_ = lv_obj_create(screen);
    lv_obj_set_size(presetPicker_, 300, 154);
    lv_obj_align(presetPicker_, LV_ALIGN_CENTER, 0, -3);
    lv_obj_set_style_bg_color(presetPicker_, lv_color_hex(0x101B21), 0);
    lv_obj_set_style_border_color(presetPicker_, lv_palette_main(LV_PALETTE_YELLOW), 0);
    lv_obj_set_style_border_width(presetPicker_, 2, 0);
    lv_obj_set_style_radius(presetPicker_, 8, 0);
    lv_obj_set_style_pad_all(presetPicker_, 0, 0);
    lv_obj_add_flag(presetPicker_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *pickerTitle = lv_label_create(presetPicker_);
    lv_label_set_text(pickerTitle, "SELECT HARDWARE PRESET");
    lv_obj_set_style_text_color(pickerTitle, lv_palette_main(LV_PALETTE_YELLOW), 0);
    lv_obj_align(pickerTitle, LV_ALIGN_TOP_MID, 0, 10);

    for (uint8_t preset = 1; preset <= 4; ++preset) {
        lv_obj_t *button = lv_button_create(presetPicker_);
        presetButtons_[preset - 1] = button;
        const int x = preset % 2 == 1 ? -72 : 72;
        const int y = preset <= 2 ? -20 : 42;
        lv_obj_set_size(button, 128, 48);
        lv_obj_align(button, LV_ALIGN_CENTER, x, y);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x28353D), 0);
        lv_obj_set_style_border_color(button, lv_color_hex(0x3B5868), 0);
        lv_obj_set_style_border_width(button, 1, 0);
        lv_obj_set_style_radius(button, 6, 0);
        lv_obj_add_event_cb(button, onPresetClicked, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<uintptr_t>(preset)));
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text_fmt(label, "PRESET %u", preset);
        lv_obj_center(label);
    }

    actionStatusLabel_ = lv_label_create(screen);
    lv_label_set_text(actionStatusLabel_, "Hardware presets ready");
    lv_obj_add_flag(actionStatusLabel_, LV_OBJ_FLAG_HIDDEN);

    detailPage_ = lv_obj_create(screen);
    lv_obj_set_size(detailPage_, 320, 180);
    lv_obj_align(detailPage_, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_bg_color(detailPage_, lv_color_hex(0x080D10), 0);
    lv_obj_set_style_border_width(detailPage_, 0, 0);
    lv_obj_set_style_radius(detailPage_, 0, 0);
    lv_obj_set_style_pad_all(detailPage_, 0, 0);
    lv_obj_remove_flag(detailPage_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(detailPage_, LV_OBJ_FLAG_HIDDEN);

    looperPage_ = createPanel(detailPage_, 6, 3, 308, 164);
    lv_obj_set_style_bg_color(looperPage_, lv_color_hex(LooperTheme::surface), 0);
    lv_obj_set_style_bg_grad_color(looperPage_, lv_color_hex(0x030A0E), 0);
    lv_obj_add_event_cb(looperPage_, drawLooperSurface, LV_EVENT_DRAW_MAIN, nullptr);
    looperBarsLabel_ = createText(looperPage_, "-- BARS", 7, 7, 66,
                                   &lv_font_montserrat_12, LooperTheme::secondary, true);
    looperBpmLabel_ = createText(looperPage_, "-- BPM", 82, 7, 68,
                                  &lv_font_montserrat_12, LooperTheme::info, true);
    // No time-signature observation exists in ControllerSnapshot yet.
    createText(looperPage_, "--/--", 160, 7, 50, &lv_font_montserrat_12, LooperTheme::muted, true);
    looperClickLabel_ = createText(looperPage_, "CLICK --", 220, 7, 78,
                                    &lv_font_montserrat_12, LooperTheme::secondary, true);
    looperStateLabel_ = createText(looperPage_, "Reading loop...", 10, 30, 286,
                                    &lv_font_montserrat_20, LooperTheme::warning);
    createText(looperPage_, "Position --", 199, 129, 98,
               &lv_font_montserrat_12, LooperTheme::muted, true);
    looperInfoLabel_ = createText(looperPage_, "Waiting for Spark status", 10, 146, 286,
                                   &lv_font_montserrat_12, LooperTheme::secondary);
    const char *looperLabels[] = {"REC /\nDUB", "PLAY", "STOP", "UNDO /\nREDO", "CLEAR"};
    const char *looperIcons[] = {LV_SYMBOL_BULLET, LV_SYMBOL_PLAY, LV_SYMBOL_STOP, LV_SYMBOL_REFRESH, LV_SYMBOL_TRASH};
    lv_obj_t **looperButtons[] = {&looperRecButton_, &looperPlayButton_, &looperStopButton_, &looperUndoButton_, &looperClearButton_};
    for (uint8_t i = 0; i < 5; ++i) {
        lv_obj_t *button = lv_button_create(looperPage_);
        *looperButtons[i] = button;
        lv_obj_set_size(button, 58, 58);
        lv_obj_set_pos(button, 2 + i * 61, 58);
        lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_pad_all(button, 0, 0);
        lv_obj_set_style_border_width(button, 2, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_style_bg_grad_dir(button, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_text_font(button, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_line_space(button, 0, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x141E25), LV_STATE_DISABLED);
        lv_obj_set_style_border_color(button, lv_color_hex(0x26343E), LV_STATE_DISABLED);
        lv_obj_set_style_text_color(button, lv_color_hex(LooperTheme::muted), LV_STATE_DISABLED);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_DISABLED);
        lv_obj_set_style_opa(button, LV_OPA_COVER, LV_STATE_DISABLED);
        lv_obj_add_event_cb(button, onLooperClicked, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
        lv_obj_t *label = lv_label_create(button);
        lv_obj_set_width(label, 54);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(label, looperLabels[i]);
        lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 26);
        lv_obj_t *icon = lv_label_create(button);
        lv_label_set_text(icon, looperIcons[i]);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 2);
    }
    lv_obj_add_flag(looperPage_, LV_OBJ_FLAG_HIDDEN);

    tunerPage_ = createPanel(detailPage_, 6, 3, 308, 160);
    lv_obj_set_style_bg_color(tunerPage_, lv_color_hex(0x0B151A), 0);
    lv_obj_set_style_bg_grad_color(tunerPage_, lv_color_hex(0x03090C), 0);
    createText(tunerPage_, "TUNER", 12, 9, 115, &lv_font_montserrat_12, 0xABBAC5);
    tunerStateLabel_ = createText(tunerPage_, "UNAVAILABLE", 180, 9, 116,
                                   &lv_font_montserrat_12, 0xD9B877);
    lv_obj_set_style_text_align(tunerStateLabel_, LV_TEXT_ALIGN_RIGHT, 0);
    tunerNoteLabel_ = createText(tunerPage_, "--", 30, 27, 246,
                                  &lv_font_montserrat_48, 0xE1E8ED, true);
    tunerMeter_ = lv_obj_create(tunerPage_);
    lv_obj_remove_style_all(tunerMeter_);
    lv_obj_set_pos(tunerMeter_, 12, 82);
    lv_obj_set_size(tunerMeter_, 282, 47);
    lv_obj_remove_flag(tunerMeter_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(tunerMeter_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(tunerMeter_, drawTunerMeter, LV_EVENT_DRAW_MAIN, &latestSnapshot_);
    createText(tunerMeter_, "-50", 1, 31, 28, &lv_font_montserrat_12, 0x7E929F, true);
    createText(tunerMeter_, "0", 130, 31, 22, &lv_font_montserrat_12, 0x7E929F, true);
    createText(tunerMeter_, "+50", 253, 31, 28, &lv_font_montserrat_12, 0x7E929F, true);
    tunerOffsetLabel_ = createText(tunerPage_, "", 10, 117, 286,
                                    &lv_font_montserrat_20, 0x4ED6F0, true);
    tunerFooter_ = lv_obj_create(tunerPage_);
    lv_obj_remove_style_all(tunerFooter_);
    lv_obj_set_pos(tunerFooter_, 10, 174);
    lv_obj_set_size(tunerFooter_, 286, 1);
    lv_obj_set_style_bg_color(tunerFooter_, lv_color_hex(0x26343D), 0);
    lv_obj_set_style_bg_opa(tunerFooter_, LV_OPA_COVER, 0);
    tunerMessageLabel_ = createText(tunerPage_, "Use the amp tuner when supported", 10, 139, 286,
                                     &lv_font_montserrat_12, 0x99ADB9, true);
    lv_obj_add_flag(tunerPage_, LV_OBJ_FLAG_HIDDEN);

    devicePage_ = lv_obj_create(detailPage_);
    lv_obj_remove_style_all(devicePage_);
    lv_obj_set_size(devicePage_, 320, 166);
    lv_obj_remove_flag(devicePage_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(devicePage_, LV_OBJ_FLAG_SCROLLABLE);
    deviceCard_ = createPanel(devicePage_, 6, 3, 308, 87);
    devicePortrait_ = lv_obj_create(deviceCard_);
    lv_obj_remove_style_all(devicePortrait_);
    lv_obj_set_pos(devicePortrait_, 7, 16);
    lv_obj_set_size(devicePortrait_, 60, 56);
    lv_obj_remove_flag(devicePortrait_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(devicePortrait_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(devicePortrait_, drawDevicePortrait, LV_EVENT_DRAW_MAIN, nullptr);
    deviceIdentityCaption_ = createText(deviceCard_, "CURRENT DEVICE", 76, 10, 218,
                                       &lv_font_montserrat_12, 0x8FA6B5);
    deviceName_ = createText(deviceCard_, "Searching for Spark", 76, 30, 218,
                             &lv_font_montserrat_20, 0xF1F4F7);
    deviceSerial_ = createText(deviceCard_, "AMP SERIAL: Not reported", 76, 59, 218,
                               &lv_font_montserrat_12, 0xB4C0CA);
    lv_obj_t *connectionPanel = createPanel(devicePage_, 6, 97, 308, 65);
    lv_obj_set_style_bg_color(connectionPanel, lv_color_hex(0x111C22), 0);
    createText(connectionPanel, LV_SYMBOL_BLUETOOTH "  Bluetooth", 12, 10, 132,
               &lv_font_montserrat_14, 0xB7C6D0);
    deviceLinkState_ = createText(connectionPanel, "Searching", 143, 10, 148,
                                  &lv_font_montserrat_14, 0xD9B877);
    lv_obj_set_style_text_align(deviceLinkState_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_t *divider = lv_obj_create(connectionPanel);
    lv_obj_remove_style_all(divider);
    lv_obj_set_pos(divider, 12, 32);
    lv_obj_set_size(divider, 282, 1);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0x2B3943), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    createText(connectionPanel, "Tone state", 12, 42, 125, &lv_font_montserrat_14, 0xB7C6D0);
    deviceToneState_ = createText(connectionPanel, "Unavailable", 143, 42, 148,
                                  &lv_font_montserrat_14, 0xD9B877);
    lv_obj_set_style_text_align(deviceToneState_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_add_flag(devicePage_, LV_OBJ_FLAG_HIDDEN);

    const int16_t detailX[] = {-104, 0, 104, -104, 0, 104};
    for (uint8_t fx = 0; fx < 6; ++fx) {
        lv_obj_t *tile = lv_obj_create(detailPage_);
        detailTiles_[fx] = tile;
        lv_obj_set_size(tile, 98, 77);
        lv_obj_align(tile, LV_ALIGN_TOP_MID, detailX[fx], fx < 3 ? 3 : 85);
        lv_obj_set_style_border_width(tile, 2, 0);
        lv_obj_set_style_radius(tile, 7, 0);
        lv_obj_set_style_pad_all(tile, 0, 0);
        lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(tile, onFxClicked, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<uintptr_t>(fx)));

        detailTileIcons_[fx] = createGlyph(tile, fx, 40, 36);
        lv_obj_align(detailTileIcons_[fx], LV_ALIGN_TOP_MID, 0, 21);

        detailTileLabels_[fx] = lv_label_create(tile);
        lv_obj_set_width(detailTileLabels_[fx], 94);
        lv_obj_set_style_text_align(detailTileLabels_[fx], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(detailTileLabels_[fx], lv_color_white(), 0);
        lv_obj_set_style_text_font(detailTileLabels_[fx], &lv_font_montserrat_14, 0);
        lv_obj_align(detailTileLabels_[fx], LV_ALIGN_TOP_MID, 0, 5);

        detailTileStateLabels_[fx] = lv_label_create(tile);
        lv_obj_set_width(detailTileStateLabels_[fx], 94);
        lv_obj_set_style_text_align(detailTileStateLabels_[fx], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(detailTileStateLabels_[fx], &lv_font_montserrat_12, 0);
        lv_obj_align(detailTileStateLabels_[fx], LV_ALIGN_BOTTOM_MID, 0, -3);
    }

    lv_obj_t *nav = lv_obj_create(screen);
    nav_ = nav;
    lv_obj_set_size(nav, 320, 30);
    lv_obj_align(nav, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(nav, lv_color_hex(0x111B21), 0);
    lv_obj_set_style_border_width(nav, 0, 0);
    lv_obj_set_style_radius(nav, 0, 0);
    lv_obj_set_style_pad_all(nav, 0, 0);
    lv_obj_remove_flag(nav, LV_OBJ_FLAG_SCROLLABLE);
    const char *navLabels[] = {"PRESET", "FX", "LOOPER", "TUNER", "DEVICE"};
    for (uint8_t i = 0; i < 5; ++i) {
        lv_obj_t *button = lv_button_create(nav);
        navButtons_[i] = button;
        lv_obj_set_size(button, 64, 30);
        lv_obj_align(button, LV_ALIGN_LEFT_MID, i * 64, 0);
        lv_obj_set_style_pad_all(button, 0, 0);
        lv_obj_set_style_radius(button, 4, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_add_event_cb(button, onNavClicked, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
        lv_obj_t *label = lv_label_create(button);
        navLabels_[i] = label;
        lv_label_set_text(label, navLabels[i]);
        lv_obj_set_width(label, 64);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(label);
        navIcons_[i] = createGlyph(button, i + 6, 20, 20);
        lv_obj_align(navIcons_[i], LV_ALIGN_TOP_MID, 0, 2);
        lv_obj_add_flag(navIcons_[i], LV_OBJ_FLAG_HIDDEN);
    }
    renderNavigation();
}

void PanelLanLVGLUI::onPresetClicked(lv_event_t *event) {
    if (!uiInstance->actions_) {
        return;
    }
    const uint8_t preset = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
    uiInstance->actions_->requestHardwarePreset(preset);
    lv_obj_add_flag(uiInstance->presetPicker_, LV_OBJ_FLAG_HIDDEN);
}

void PanelLanLVGLUI::onPresetCardClicked(lv_event_t *) {
    lv_obj_remove_flag(uiInstance->presetPicker_, LV_OBJ_FLAG_HIDDEN);
}

void PanelLanLVGLUI::onFxClicked(lv_event_t *event) {
    if (!uiInstance->actions_ || uiInstance->activeScreen_ != Screen::Fx) {
        return;
    }
    const uint8_t slot = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
    uiInstance->actions_->requestFxToggle(slot);
}

void PanelLanLVGLUI::onNavClicked(lv_event_t *event) {
    if (uiInstance->latestSnapshot_.tunerActive) {
        const uint8_t page = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
        if (page <= static_cast<uint8_t>(Screen::Device) && uiInstance->actions_) {
            // Keep the nav labels stable while tuner owns the amp. Any
            // destination first exits tuner, then becomes the restored view.
            // TUNER itself simply returns to the pre-tuner screen.
            if (page != static_cast<uint8_t>(Screen::Tuner)) {
                uiInstance->screenBeforeTuner_ = static_cast<Screen>(page);
            }
            uiInstance->actions_->requestTuner(false);
        }
        return;
    }
    const uint8_t page = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
    if (page == static_cast<uint8_t>(Screen::Looper) &&
        uiInstance->latestSnapshot_.looperCapability != ControllerLooperCapability::Verified) return;
    if (page <= static_cast<uint8_t>(Screen::Device)) {
        uiInstance->setActiveScreen(static_cast<Screen>(page));
        if (page == static_cast<uint8_t>(Screen::Tuner) && uiInstance->actions_) {
            uiInstance->actions_->requestTuner(true);
        }
    }
}

void PanelLanLVGLUI::onLooperClicked(lv_event_t *event) {
    if (!uiInstance->actions_ || uiInstance->activeScreen_ != Screen::Looper) return;
    switch (static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)))) {
    case 0: uiInstance->actions_->requestLooperRecordDub(); break;
    case 1: uiInstance->actions_->requestLooperPlay(); break;
    case 2: uiInstance->actions_->requestLooperStop(); break;
    case 3: uiInstance->actions_->requestLooperUndoRedo(); break;
    case 4: uiInstance->actions_->requestLooperClear(); break;
    }
}

void PanelLanLVGLUI::setActiveScreen(Screen screen) {
    if (tunerOverrideActive_ && screen != Screen::Tuner) {
        return;
    }
    if (activeScreen_ == Screen::Looper && screen != Screen::Looper && actions_) {
        actions_->cancelLooperClear();
    }
    activeScreen_ = screen;
    if (screen == Screen::Preset) {
        lv_obj_add_flag(detailPage_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(presetPicker_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(detailPage_, LV_OBJ_FLAG_HIDDEN);
        renderDetailPage(latestSnapshot_);
    }
    renderNavigation();
}

void PanelLanLVGLUI::renderNavigation() {
    lv_obj_remove_flag(nav_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(nav_, 40);
    lv_label_set_text(headerTitle_, latestSnapshot_.identityKnown
                                        ? latestSnapshot_.ampName.c_str() : "IGNITRON");
    lv_obj_set_width(headerTitle_, 156);
    lv_label_set_long_mode(headerTitle_, LV_LABEL_LONG_DOT);
    for (uint8_t i = 0; i < 5; ++i) {
        const bool selected = i == static_cast<uint8_t>(activeScreen_);
        const bool looperAvailable = i != static_cast<uint8_t>(Screen::Looper) ||
                                     latestSnapshot_.looperCapability == ControllerLooperCapability::Verified;
        lv_obj_set_height(navButtons_[i], 40);
        lv_obj_set_style_bg_color(navButtons_[i], selected ? lv_color_hex(0x25251C)
                                                          : lv_color_hex(0x090F13), 0);
        lv_obj_set_style_bg_grad_color(navButtons_[i], lv_color_hex(0x080D10), 0);
        lv_obj_set_style_bg_grad_dir(navButtons_[i], LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_border_color(navButtons_[i], selected ? lv_palette_main(LV_PALETTE_YELLOW)
                                                               : lv_color_hex(0x111B21), 0);
        lv_obj_set_style_border_width(navButtons_[i], selected ? 1 : 0, 0);
        lv_obj_set_style_text_color(navLabels_[i], selected ? lv_palette_main(LV_PALETTE_YELLOW)
                                                            : lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
        lv_obj_set_style_text_font(navLabels_[i], &lv_font_montserrat_12, 0);
        static const char *kNavLabels[] = {"PRESET", "FX", "LOOPER", "TUNER", "DEVICE"};
        lv_label_set_text(navLabels_[i], kNavLabels[i]);
        if (looperAvailable) lv_obj_remove_state(navButtons_[i], LV_STATE_DISABLED);
        else lv_obj_add_state(navButtons_[i], LV_STATE_DISABLED);
        lv_obj_remove_flag(navIcons_[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(navIcons_[i], selected ? lv_color_hex(0xFFD65A) : lv_color_hex(0xA7B7C3), 0);
        lv_obj_align(navLabels_[i], LV_ALIGN_BOTTOM_MID, 0, -3);
    }
}

void PanelLanLVGLUI::renderDetailPage(const ControllerSnapshot &snapshot) {
    const bool isFxPage = activeScreen_ == Screen::Fx;
    lv_obj_set_height(detailPage_, 180);
    auto showOnly = [](lv_obj_t *page, bool visible) {
        if (visible) lv_obj_remove_flag(page, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    };
    showOnly(looperPage_, activeScreen_ == Screen::Looper);
    showOnly(tunerPage_, activeScreen_ == Screen::Tuner);
    showOnly(devicePage_, activeScreen_ == Screen::Device);
    for (uint8_t fx = 0; fx < 6; ++fx) {
        if (isFxPage) {
            lv_obj_remove_flag(detailTiles_[fx], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(detailTiles_[fx], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (isFxPage) {
        // The concept gives the entire body to six effects. Status already
        // belongs to the persistent header; an extra title shrinks the cards.
        lv_label_set_text(headerTitle_, snapshot.identityKnown ? snapshot.ampName.c_str() : "IGNITRON");
        static const lv_color_t kFxAccents[] = {
            lv_color_hex(0x00E65D), lv_color_hex(0x48D8BC), lv_color_hex(0xFF3044),
            lv_color_hex(0xA497E9), lv_color_hex(0x00ABFF), lv_color_hex(0xA99BEA),
        };
        static const lv_color_t kFxOnBackgrounds[] = {
            lv_color_hex(0x07532E), lv_color_hex(0x164C43), lv_color_hex(0x611823),
            lv_color_hex(0x38304E), lv_color_hex(0x064775), lv_color_hex(0x39304E),
        };
        static const char *kFxNames[] = {"GATE", "COMP", "DRIVE", "MOD", "DELAY", "REVERB"};
        for (uint8_t fx = 0; fx < 6; ++fx) {
            const ControllerFxSlot &slot = snapshot.fxSlots[fx];
            const bool needsSync = snapshot.sparkStateStale || !slot.known;
            // Pending and failed requests deliberately retain the confirmed
            // card body. Only ControllerState can change `enabled`; this
            // renderer only makes the trust level legible.
            const bool confirmedEnabled = slot.known && slot.enabled;
            const bool pending = !needsSync && slot.pending;
            const bool failed = !needsSync && !pending && slot.actionFailed;
            const char *name = slot.known ? slot.label.c_str() : kFxNames[fx];
            lv_label_set_text(detailTileLabels_[fx], name);
            const lv_color_t confirmedAccent = confirmedEnabled ? kFxAccents[fx] : lv_color_hex(0x96A5B7);
            lv_color_t border = confirmedEnabled ? confirmedAccent : lv_color_hex(0x4B5965);
            lv_color_t iconColor = confirmedAccent;
            lv_color_t stateColor = lv_color_hex(0xDCE7EF);
            const char *state = confirmedEnabled ? "ON" : "OFF";
            lv_color_t background = confirmedEnabled ? kFxOnBackgrounds[fx] : lv_color_hex(0x242D35);
            lv_color_t gradient = confirmedEnabled
                                      ? lv_color_mix(kFxOnBackgrounds[fx], lv_color_black(), 110)
                                      : lv_color_hex(0x101820);

            // Trust state takes precedence over the apparent effect state:
            // a stale/unknown Spark observation must never look like a
            // confirmed bypass or enabled pedal.
            if (needsSync) {
                state = "SYNC";
                background = lv_color_hex(0x242D35);
                gradient = lv_color_hex(0x101820);
                border = lv_color_hex(0x7B6135);
                iconColor = lv_color_hex(0xD9B877);
                stateColor = lv_color_hex(0xD9B877);
            } else if (pending) {
                state = slot.pendingDesiredEnabled ? "TURNING ON" : "TURNING OFF";
                border = lv_color_hex(0xD9B877);
                iconColor = lv_color_hex(0xFFD06A);
                stateColor = lv_color_hex(0xFFD06A);
            } else if (failed) {
                state = "RETRY";
                border = lv_color_hex(0xFF5965);
                iconColor = lv_color_hex(0xFF6C76);
                stateColor = lv_color_hex(0xFF6C76);
            }

            lv_label_set_text(detailTileStateLabels_[fx], state);
            lv_obj_set_style_bg_color(detailTiles_[fx], background, 0);
            lv_obj_set_style_bg_grad_color(detailTiles_[fx], gradient, 0);
            lv_obj_set_style_bg_grad_dir(detailTiles_[fx], LV_GRAD_DIR_VER, 0);
            lv_obj_set_style_border_color(detailTiles_[fx], border, 0);
            lv_obj_set_style_text_color(detailTileIcons_[fx], iconColor, 0);
            lv_obj_invalidate(detailTileIcons_[fx]);
            lv_obj_set_style_text_color(detailTileLabels_[fx], lv_color_hex(0xF1F4F7), 0);
            lv_obj_set_style_text_color(detailTileStateLabels_[fx], stateColor, 0);
        }
        return;
    }
    if (activeScreen_ == Screen::Tuner) {
        lv_obj_set_height(tunerPage_, 160);
        lv_obj_set_y(tunerNoteLabel_, snapshot.tunerActive ? 18 : 36);
        lv_obj_set_y(tunerMeter_, 68);
        lv_obj_set_y(tunerOffsetLabel_, 117);
        lv_obj_set_y(tunerFooter_, 141);
        lv_obj_set_y(tunerMessageLabel_, snapshot.tunerActive ? 146 : 136);
        showOnly(tunerMeter_, snapshot.tunerActive);
        showOnly(tunerFooter_, snapshot.tunerActive);
        showOnly(tunerOffsetLabel_, snapshot.tunerActive);
        lv_obj_invalidate(tunerMeter_);
        if (!snapshot.tunerActive) {
            lv_label_set_text(tunerStateLabel_, "READY");
            lv_obj_set_style_text_color(tunerStateLabel_, lv_color_hex(0xD9B877), 0);
            lv_label_set_text(tunerNoteLabel_, "--");
            lv_obj_set_style_text_color(tunerNoteLabel_, lv_color_hex(0xA4B3BE), 0);
            lv_label_set_text(tunerOffsetLabel_, "");
            lv_label_set_text(tunerMessageLabel_, "Tap TUNER to enter tuner mode");
        } else if (snapshot.tunerSampleFresh && !snapshot.tunerNote.empty() && snapshot.tunerNote != " ") {
            lv_label_set_text(tunerStateLabel_, "ACTIVE");
            lv_obj_set_style_text_color(tunerStateLabel_, lv_color_hex(0x00E65D), 0);
            lv_label_set_text(tunerNoteLabel_, snapshot.tunerNote.c_str());
            lv_obj_set_style_text_color(tunerNoteLabel_, lv_color_hex(0xF1F4F7), 0);
            lv_obj_set_style_text_color(tunerOffsetLabel_, lv_color_hex(0x4ED6F0), 0);
            // LVGL's compact formatter does not include float formatting on
            // this target. Render the parsed raw offset with integer pieces
            // so a live sample can never leave a literal "%f" on screen.
            const int cents = snapshot.tunerOffsetCents;
            lv_label_set_text_fmt(tunerOffsetLabel_, "%+d cents", cents);
            lv_label_set_text(tunerMessageLabel_, "Tap EXIT to return to your preset");
        } else {
            lv_label_set_text(tunerStateLabel_, "ACTIVE");
            lv_obj_set_style_text_color(tunerStateLabel_, lv_color_hex(0x00E65D), 0);
            lv_label_set_text(tunerNoteLabel_, "--");
            lv_obj_set_style_text_color(tunerNoteLabel_, lv_color_hex(0x526874), 0);
            lv_obj_set_style_text_color(tunerOffsetLabel_, lv_color_hex(0xD9B877), 0);
            lv_label_set_text(tunerOffsetLabel_, "LISTENING");
            lv_label_set_text(tunerMessageLabel_, "Play a note on your guitar");
        }
        return;
    }
    if (activeScreen_ == Screen::Looper) {
        const bool supported = snapshot.looperCapability == ControllerLooperCapability::Verified;
        const bool linked = snapshot.connectionPhase == ControllerConnectionPhase::Ready ||
                            snapshot.connectionPhase == ControllerConnectionPhase::Syncing ||
                            snapshot.connectionPhase == ControllerConnectionPhase::Identifying;
        // A fresh status proves loop existence even when Spark has not sent
        // a transport notification. Do not lock out PLAY, Undo or Clear just
        // because Playing versus Stopped remains unknown after reconnect.
        const bool ready = supported && snapshot.looperKnown && !snapshot.looperStale;
        const bool hasLoop = snapshot.looperLoopCount > 0;
        const bool recording = snapshot.looperTransport == ControllerLooperTransport::Recording;
        const bool overdubbing = snapshot.looperTransport == ControllerLooperTransport::Overdubbing;
        const bool settingsFresh = linked && supported && snapshot.looperSettingsKnown && !snapshot.looperStale;
        if (settingsFresh && snapshot.looperBars > 0) {
            lv_label_set_text_fmt(looperBarsLabel_, "%d %s", snapshot.looperBars,
                                  snapshot.looperBars == 1 ? "BAR" : "BARS");
        } else lv_label_set_text(looperBarsLabel_, "-- BARS");
        if (settingsFresh && snapshot.looperBpm > 0) {
            lv_label_set_text_fmt(looperBpmLabel_, "%d BPM", snapshot.looperBpm);
        } else lv_label_set_text(looperBpmLabel_, "-- BPM");
        lv_label_set_text(looperClickLabel_, !settingsFresh ? "CLICK --" : snapshot.looperClick ? "CLICK ON" : "CLICK OFF");
        lv_obj_set_style_text_color(looperClickLabel_, lv_color_hex(settingsFresh && snapshot.looperClick
                                                                      ? LooperTheme::play : LooperTheme::secondary), 0);
        const char *transport = hasLoop ? "Loop available" : "State unknown";
        const char *guidance = hasLoop ? "Transport unknown - PLAY or STOP" : "Waiting for Spark status";
        uint32_t stateColor = LooperTheme::text;
        switch (snapshot.looperTransport) {
        case ControllerLooperTransport::Empty:
            transport = "No loop yet";
            guidance = "Tap REC to start recording";
            break;
        case ControllerLooperTransport::Stopped:
            transport = "Loop stopped";
            guidance = "Play your loop or add another layer";
            break;
        case ControllerLooperTransport::Recording:
            transport = "Recording...";
            guidance = "Finish below to start playback";
            stateColor = LooperTheme::record;
            break;
        case ControllerLooperTransport::Playing:
            transport = "Playing loop";
            guidance = "Overdub adds a layer as the loop plays";
            stateColor = LooperTheme::play;
            break;
        case ControllerLooperTransport::Overdubbing:
            transport = "Overdubbing...";
            guidance = "Finish keeps your loop playing";
            stateColor = LooperTheme::record;
            break;
        default: break;
        }
        uint32_t infoColor = LooperTheme::secondary;
        uint32_t borderColor = LooperTheme::border;
        if (!linked) {
            transport = "Not connected";
            guidance = "Reconnect Spark to use the looper";
            stateColor = LooperTheme::warning;
        } else if (!supported) {
            transport = "Unavailable";
            guidance = "Looper needs a verified Spark 2";
            stateColor = LooperTheme::muted;
        } else if (!ready) {
            transport = snapshot.looperKnown && snapshot.looperStale ? "State out of date" : "State unknown";
            guidance = "Waiting for fresh Spark status";
            stateColor = LooperTheme::warning;
        } else if (snapshot.looperPending) {
            // Keep the last confirmed transport visible; the request is only
            // an amber annotation until a Spark observation confirms it.
            guidance = "Pending - waiting for Spark to confirm";
            infoColor = borderColor = LooperTheme::warning;
        } else if (snapshot.looperClearArmed) {
            transport = "Clear this loop?";
            guidance = "Tap Clear again within 3 sec to erase";
            stateColor = infoColor = borderColor = LooperTheme::record;
        } else if (snapshot.looperActionFailed) {
            guidance = "Not confirmed - refreshing Spark state";
            infoColor = borderColor = LooperTheme::record;
        } else if (snapshot.connectionPhase != ControllerConnectionPhase::Ready || snapshot.sparkStateStale) {
            guidance = "Syncing Spark - controls will unlock";
            infoColor = LooperTheme::warning;
        }
        lv_label_set_text(looperStateLabel_, transport);
        lv_label_set_text(looperInfoLabel_, guidance);
        lv_obj_set_style_text_color(looperStateLabel_, lv_color_hex(stateColor), 0);
        lv_obj_set_style_text_color(looperInfoLabel_, lv_color_hex(infoColor), 0);
        lv_obj_set_style_border_color(looperPage_, lv_color_hex(borderColor), 0);

        const char *recordLabel = !ready || snapshot.looperTransport == ControllerLooperTransport::Unknown
                                      ? "REC /\nDUB" : recording || overdubbing ? "FINISH" : hasLoop ? "DUB" : "REC";
        lv_label_set_text(lv_obj_get_child(looperRecButton_, 0), recordLabel);
        lv_label_set_text(lv_obj_get_child(looperClearButton_, 0), snapshot.looperClearArmed ? "AGAIN" : "CLEAR");
        lv_label_set_text(lv_obj_get_child(looperRecButton_, 1), ready && (recording || overdubbing) ? LV_SYMBOL_OK : LV_SYMBOL_BULLET);
        styleLooperButton(looperRecButton_, recording || overdubbing ? LooperTheme::playFill : LooperTheme::recordFill,
                          recording || overdubbing ? LooperTheme::play : LooperTheme::record);
        styleLooperButton(looperPlayButton_, LooperTheme::playFill, LooperTheme::play);
        styleLooperButton(looperStopButton_, 0x273846, LooperTheme::warning);
        styleLooperButton(looperUndoButton_, 0x253039, LooperTheme::secondary);
        styleLooperButton(looperClearButton_, snapshot.looperClearArmed ? LooperTheme::dangerFill : LooperTheme::surface,
                          snapshot.looperClearArmed ? LooperTheme::record : LooperTheme::secondary);
        const bool actionReady = ready && !snapshot.looperPending &&
                                 snapshot.connectionPhase == ControllerConnectionPhase::Ready &&
                                 !snapshot.sparkStateStale && !snapshot.tunerActive && snapshot.pendingHardwarePreset == 0;
        auto enable = [](lv_obj_t *button, bool enabled) {
            if (enabled) lv_obj_remove_state(button, LV_STATE_DISABLED);
            else {
                lv_obj_add_state(button, LV_STATE_DISABLED);
                lv_obj_set_style_text_color(lv_obj_get_child(button, 1), lv_color_hex(LooperTheme::muted), 0);
            }
        };
        enable(looperRecButton_, actionReady);
        // Loop-count-dependent actions do not require a complete transport
        // observation; count-only status must leave existing loops usable.
        enable(looperPlayButton_, actionReady && hasLoop);
        enable(looperStopButton_, actionReady && hasLoop);
        enable(looperUndoButton_, actionReady && hasLoop);
        enable(looperClearButton_, actionReady && hasLoop);
        return;
    }
    if (activeScreen_ == Screen::Device) {
        const bool linked = snapshot.connectionPhase == ControllerConnectionPhase::Identifying ||
                            snapshot.connectionPhase == ControllerConnectionPhase::Syncing ||
                            snapshot.connectionPhase == ControllerConnectionPhase::Ready;
        const bool hasName = !snapshot.ampName.empty();
        const bool known = snapshot.identityKnown;
        lv_label_set_text(deviceIdentityCaption_, known ? "CURRENT DEVICE"
                                                       : hasName ? "LAST SEEN DEVICE" : "SPARK CONNECTION");
        lv_label_set_text(deviceName_, hasName ? snapshot.ampName.c_str()
                                             : linked ? "Identifying Spark" : "Looking for Spark");
        lv_obj_set_style_text_color(deviceName_, known ? lv_color_hex(0xF1F4F7)
                                                       : lv_color_hex(0x98A9B6), 0);
        lv_label_set_text_fmt(deviceSerial_, "AMP SERIAL: %s", known && !snapshot.ampSerial.empty()
                                                               ? snapshot.ampSerial.c_str() : "Not reported");
        lv_obj_set_user_data(devicePortrait_, snapshot.ampName.find("NEO") != std::string::npos
                                                 ? reinterpret_cast<void *>(1) : nullptr);
        lv_obj_invalidate(devicePortrait_);
        const uint32_t linkColor = linked ? 0x00E65D : 0xD9B877;
        lv_obj_set_style_border_color(deviceCard_, lv_color_hex(linked ? 0x376353 : 0x34434D), 0);
        lv_label_set_text(deviceLinkState_, linked ? "Connected"
             : snapshot.connectionPhase == ControllerConnectionPhase::Reconnecting ? "Reconnecting" : "Searching");
        lv_obj_set_style_text_color(deviceLinkState_, lv_color_hex(linkColor), 0);
        const char *toneState = !linked ? (snapshot.confirmedHardwarePreset ? "Last known / stale" : "Unavailable")
                                       : snapshot.sparkStateStale ? "Synchronizing" : "Current";
        lv_label_set_text(deviceToneState_, toneState);
        lv_obj_set_style_text_color(deviceToneState_, lv_color_hex(snapshot.sparkStateStale ? 0xD9B877 : 0x00E65D), 0);
    }
}

void PanelLanLVGLUI::reconcileExternalTuner(const ControllerSnapshot &snapshot) {
    if (snapshot.tunerActive && !tunerOverrideActive_) {
        // Preserve only a performance screen. A manual unavailable Tuner tab
        // is not a useful place to return after the amp exits real tuner mode.
        screenBeforeTuner_ = activeScreen_ == Screen::Tuner ? Screen::Preset : activeScreen_;
        tunerOverrideActive_ = true;
        setActiveScreen(Screen::Tuner);
    } else if (!snapshot.tunerActive && tunerOverrideActive_) {
        tunerOverrideActive_ = false;
        setActiveScreen(screenBeforeTuner_);
    }
}

bool PanelLanLVGLUI::writeScreenshot(Stream &output) {
    const size_t captureBytes = kDisplayWidth * kDisplayHeight * sizeof(uint16_t);
    uint8_t *capture = static_cast<uint8_t *>(heap_caps_malloc(captureBytes, MALLOC_CAP_SPIRAM));
    lv_image_dsc_t snapshot{};
    const lv_result_t result = capture
                                   ? lv_snapshot_take_to_buf(lv_screen_active(), LV_COLOR_FORMAT_RGB565,
                                                             &snapshot, capture, captureBytes)
                                   : LV_RESULT_INVALID;
    if (result != LV_RESULT_OK || snapshot.header.w != kDisplayWidth || snapshot.header.h != kDisplayHeight) {
        if (capture) {
            heap_caps_free(capture);
        }
        output.println("IGNITRON_SCREENSHOT_ERROR");
        return false;
    }

    // PPM is intentionally simple: the host capture tool can save it without
    // a graphics dependency, then compare the physical LVGL composition with
    // the concept image.
    output.printf("IGNITRON_SCREENSHOT_PPM %u %u\nP6\n%u %u\n255\n",
                  kDisplayWidth, kDisplayHeight, kDisplayWidth, kDisplayHeight);
    uint8_t row[kDisplayWidth * 3];
    const uint16_t *pixels = reinterpret_cast<const uint16_t *>(snapshot.data);
    for (uint16_t y = 0; y < kDisplayHeight; ++y) {
        for (uint16_t x = 0; x < kDisplayWidth; ++x) {
            const uint16_t pixel = pixels[y * kDisplayWidth + x];
            row[x * 3] = (pixel >> 8) & 0xF8;
            row[x * 3 + 1] = (pixel >> 3) & 0xFC;
            row[x * 3 + 2] = (pixel << 3) & 0xF8;
        }
        output.write(row, sizeof(row));
        delay(0);
    }
    heap_caps_free(capture);
    return true;
}

void PanelLanLVGLUI::renderStatus(const ControllerSnapshot &snapshot) {
    latestSnapshot_ = snapshot;
    reconcileExternalTuner(snapshot);
    const bool linkEstablished = snapshot.connectionPhase == ControllerConnectionPhase::Identifying ||
                                 snapshot.connectionPhase == ControllerConnectionPhase::Syncing ||
                                 snapshot.connectionPhase == ControllerConnectionPhase::Ready;
    const lv_color_t accent = linkEstablished ? lv_palette_main(LV_PALETTE_GREEN)
                                               : lv_palette_main(LV_PALETTE_ORANGE);
    const char *connectionText = snapshot.connectionPhase == ControllerConnectionPhase::Reconnecting
                                     ? "RECONNECTING"
                                     : snapshot.connectionPhase == ControllerConnectionPhase::Ready ? LV_SYMBOL_BLUETOOTH " CONNECTED"
                                     : linkEstablished ? LV_SYMBOL_BLUETOOTH " SYNCING" : "SEARCHING";
    lv_label_set_text(connectionLabel_, connectionText);
    lv_obj_set_style_text_color(connectionLabel_, accent, 0);
    lv_obj_set_style_border_color(lv_obj_get_parent(connectionLabel_), accent, 0);
    lv_label_set_text(headerTitle_, snapshot.identityKnown ? snapshot.ampName.c_str() : "IGNITRON");
    if (snapshot.pendingHardwarePreset != 0) {
        lv_label_set_text_fmt(identityLabel_, "SELECTING PRESET %u", snapshot.pendingHardwarePreset);
    } else if (snapshot.confirmedHardwarePreset != 0) {
        lv_label_set_text_fmt(identityLabel_, snapshot.sparkStateStale ? "PRESET %u / SYNCING"
                                                                      : "HARDWARE PRESET %u",
                              snapshot.confirmedHardwarePreset);
    } else if (linkEstablished) {
        lv_label_set_text(identityLabel_, "READING YOUR PRESET");
    } else {
        lv_label_set_text(identityLabel_, "SPARK PEDAL CONTROLLER");
    }

    if (!snapshot.presetName.empty()) {
        lv_label_set_text(presetNameLabel_, snapshot.presetName.c_str());
    } else if (snapshot.confirmedHardwarePreset != 0) {
        lv_label_set_text_fmt(presetNameLabel_, "PRESET %u", snapshot.confirmedHardwarePreset);
    } else {
        lv_label_set_text(presetNameLabel_, linkEstablished ? "SYNCING PRESET" : "YOUR TONE");
    }
    lv_obj_set_style_text_color(presetNameLabel_, snapshot.sparkStateStale ? lv_color_hex(0xA8B3BC)
                                                                        : lv_color_hex(0xF4F6F8), 0);
    const bool hasUsefulDescription = !snapshot.presetDescription.empty() && snapshot.presetDescription != "Text";
    lv_label_set_text(presetDescriptionLabel_, snapshot.presetActionFailed ? "Change not confirmed. Tap to retry."
                                            : !linkEstablished ? "Turn on your Spark device"
                                            : snapshot.sparkStateStale ? "Waiting for current Spark state"
                                            : hasUsefulDescription
                                                ? snapshot.presetDescription.c_str()
                                                : "Tap to choose a preset");

    for (uint8_t preset = 1; preset <= 4; ++preset) {
        lv_obj_t *button = presetButtons_[preset - 1];
        // Performance UI is intentionally optimistic: selection moves at tap
        // time, while ControllerState still retains the confirmed value and
        // restores it if Spark does not verify the change.
        const uint8_t displayedPreset = snapshot.pendingHardwarePreset != 0
                                           ? snapshot.pendingHardwarePreset
                                           : snapshot.confirmedHardwarePreset;
        const bool selected = displayedPreset == preset;
        const bool ready = snapshot.connectionPhase == ControllerConnectionPhase::Ready;
        const lv_color_t color = selected ? lv_palette_main(LV_PALETTE_GREEN)
                               : ready ? lv_palette_main(LV_PALETTE_BLUE)
                                         : lv_color_hex(0x28353D);
        lv_obj_set_style_bg_color(button, color, 0);
        // Input acceptance is owned by ControllerActions. Do not use LVGL's
        // disabled state here: its dimming makes the still-valid alternatives
        // look broken while a request is pending.
        lv_obj_remove_state(button, LV_STATE_DISABLED);
    }

    static const lv_color_t kFxColors[] = {
        lv_color_hex(0x00E65D), lv_color_hex(0x48D8BC), lv_color_hex(0xFF3044),
        lv_color_hex(0xA497E9), lv_color_hex(0x00ABFF), lv_color_hex(0xA99BEA),
    };
    static const char *kFxNames[] = {"Gate", "Comp", "Drive", "Mod", "Delay", "Reverb"};
    for (uint8_t fx = 0; fx < 6; ++fx) {
        const ControllerFxSlot &slot = snapshot.fxSlots[fx];
        const bool current = slot.known && !snapshot.sparkStateStale;
        const bool enabled = current && slot.enabled;
        lv_obj_t *tile = fxTiles_[fx];
        lv_obj_t *label = lv_obj_get_child(tile, 0);
        lv_label_set_text(label, kFxNames[fx]);
        // Unknown/stale is visibly distinct from a confirmed bypassed effect.
        lv_label_set_text(fxStateLabels_[fx], current ? "" : "?");
        lv_obj_set_style_text_color(fxStateLabels_[fx], lv_color_hex(0xD9B877), 0);
        lv_obj_set_style_bg_color(tile, enabled ? lv_color_mix(kFxColors[fx], lv_color_black(), 65)
                                              : lv_color_hex(0x242D35), 0);
        lv_obj_set_style_bg_grad_color(tile, enabled ? lv_color_mix(kFxColors[fx], lv_color_black(), 20)
                                                   : lv_color_hex(0x101820), 0);
        lv_obj_set_style_bg_grad_dir(tile, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_border_color(tile, enabled ? kFxColors[fx] : lv_color_hex(0x4B5965), 0);
        lv_obj_set_style_text_color(fxIcons_[fx], enabled ? kFxColors[fx] : lv_color_hex(0x96A5B7), 0);
        lv_obj_invalidate(fxIcons_[fx]);
    }

    if (snapshot.pendingHardwarePreset != 0) {
        lv_label_set_text(actionStatusLabel_, "Switching preset...");
    } else if (snapshot.presetActionFailed) {
        lv_label_set_text(actionStatusLabel_, "Preset switch was not confirmed");
    } else {
        lv_label_set_text(actionStatusLabel_, "Hardware presets ready");
    }
    if (activeScreen_ != Screen::Preset) {
        renderDetailPage(snapshot);
    }
}

void PanelLanLVGLUI::update(const ControllerSnapshot &snapshot) {
    const uint32_t now = millis();
    lv_tick_inc(now - lastLvglTickAt_);
    lastLvglTickAt_ = now;
    if (renderedRevision_ != snapshot.revision) {
        renderedRevision_ = snapshot.revision;
        renderStatus(snapshot);
    }
    lv_timer_handler();
}

#endif
