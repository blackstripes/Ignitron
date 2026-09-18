#include "PanelLanLVGLUI.h"
#include "controller/ControllerActions.h"

#include <esp_heap_caps.h>

#if defined(PANELAN_SC05X_MODE) && defined(PANELAN_LVGL_UI_MODE)

namespace {
PanelLanLVGLUI *uiInstance = nullptr;

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

    detailTitle_ = lv_label_create(detailPage_);
    lv_obj_set_style_text_color(detailTitle_, lv_color_white(), 0);
    lv_obj_set_style_text_font(detailTitle_, &lv_font_montserrat_20, 0);
    lv_obj_align(detailTitle_, LV_ALIGN_TOP_LEFT, 12, 10);

    detailMessage_ = lv_label_create(detailPage_);
    lv_obj_set_width(detailMessage_, 292);
    lv_label_set_long_mode(detailMessage_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(detailMessage_, lv_palette_lighten(LV_PALETTE_GREY, 2), 0);
    lv_obj_align(detailMessage_, LV_ALIGN_TOP_LEFT, 14, 42);

    const int16_t detailX[] = {-104, 0, 104, -104, 0, 104};
    for (uint8_t fx = 0; fx < 6; ++fx) {
        lv_obj_t *tile = lv_obj_create(detailPage_);
        detailTiles_[fx] = tile;
        lv_obj_set_size(tile, 98, 77);
        lv_obj_align(tile, LV_ALIGN_TOP_MID, detailX[fx], fx < 3 ? 3 : 85);
        lv_obj_set_style_border_width(tile, 2, 0);
        lv_obj_set_style_radius(tile, 7, 0);
        lv_obj_set_style_pad_all(tile, 0, 0);
        lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

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

void PanelLanLVGLUI::onNavClicked(lv_event_t *event) {
    const uint8_t page = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
    if (page <= static_cast<uint8_t>(Screen::Device)) {
        uiInstance->setActiveScreen(static_cast<Screen>(page));
    }
}

void PanelLanLVGLUI::setActiveScreen(Screen screen) {
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
    const bool conceptFx = activeScreen_ == Screen::Fx || activeScreen_ == Screen::Preset;
    lv_obj_set_height(nav_, conceptFx ? 40 : 30);
    lv_label_set_text(headerTitle_, conceptFx && latestSnapshot_.identityKnown
                                        ? latestSnapshot_.ampName.c_str() : "IGNITRON");
    lv_obj_set_width(headerTitle_, conceptFx ? 156 : 100);
    lv_label_set_long_mode(headerTitle_, LV_LABEL_LONG_DOT);
    for (uint8_t i = 0; i < 5; ++i) {
        const bool selected = i == static_cast<uint8_t>(activeScreen_);
        lv_obj_set_height(navButtons_[i], conceptFx ? 40 : 30);
        lv_obj_set_style_bg_color(navButtons_[i], conceptFx
            ? (selected ? lv_color_hex(0x25251C) : lv_color_hex(0x090F13))
            : (selected ? lv_color_hex(0x1E2020) : lv_color_hex(0x111B21)), 0);
        lv_obj_set_style_bg_grad_color(navButtons_[i], lv_color_hex(0x080D10), 0);
        lv_obj_set_style_bg_grad_dir(navButtons_[i], conceptFx ? LV_GRAD_DIR_VER : LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_border_color(navButtons_[i], selected ? lv_palette_main(LV_PALETTE_YELLOW)
                                                               : lv_color_hex(0x111B21), 0);
        lv_obj_set_style_border_width(navButtons_[i], selected ? 1 : 0, 0);
        lv_obj_set_style_text_color(navLabels_[i], selected ? lv_palette_main(LV_PALETTE_YELLOW)
                                                            : lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
        lv_obj_set_style_text_font(navLabels_[i], conceptFx ? &lv_font_montserrat_12 : &lv_font_montserrat_14, 0);
        if (conceptFx) {
            lv_obj_remove_flag(navIcons_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_color(navIcons_[i], selected ? lv_color_hex(0xFFD65A) : lv_color_hex(0xA7B7C3), 0);
            lv_obj_align(navLabels_[i], LV_ALIGN_BOTTOM_MID, 0, -3);
        } else {
            lv_obj_add_flag(navIcons_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_center(navLabels_[i]);
        }
    }
}

void PanelLanLVGLUI::renderDetailPage(const ControllerSnapshot &snapshot) {
    const bool isFxPage = activeScreen_ == Screen::Fx;
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
        lv_obj_add_flag(detailTitle_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(detailMessage_, LV_OBJ_FLAG_HIDDEN);
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
            const bool current = slot.known && !snapshot.sparkStateStale;
            const bool enabled = current && slot.enabled;
            const lv_color_t accent = enabled ? kFxAccents[fx] : lv_color_hex(0x96A5B7);
            const char *name = slot.known ? slot.label.c_str() : kFxNames[fx];
            lv_label_set_text(detailTileLabels_[fx], name);
            lv_label_set_text(detailTileStateLabels_[fx], current ? (enabled ? "ON" : "OFF") : "SYNC");
            lv_obj_set_style_bg_color(detailTiles_[fx], enabled ? kFxOnBackgrounds[fx] : lv_color_hex(0x242D35), 0);
            lv_obj_set_style_bg_grad_color(detailTiles_[fx], enabled ? lv_color_mix(kFxOnBackgrounds[fx], lv_color_black(), 110)
                                                                 : lv_color_hex(0x101820), 0);
            lv_obj_set_style_bg_grad_dir(detailTiles_[fx], LV_GRAD_DIR_VER, 0);
            lv_obj_set_style_border_color(detailTiles_[fx], enabled ? accent : lv_color_hex(0x4B5965), 0);
            lv_obj_set_style_text_color(detailTileIcons_[fx], accent, 0);
            lv_obj_invalidate(detailTileIcons_[fx]);
            lv_obj_set_style_text_color(detailTileLabels_[fx], lv_color_hex(0xF1F4F7), 0);
            lv_obj_set_style_text_color(detailTileStateLabels_[fx], current ? lv_color_hex(0xDCE7EF)
                                                                           : lv_color_hex(0xD9B877), 0);
        }
        return;
    }
    lv_obj_remove_flag(detailTitle_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(detailMessage_, LV_OBJ_FLAG_HIDDEN);
    switch (activeScreen_) {
    case Screen::Looper:
        lv_label_set_text(detailTitle_, "LOOPER");
        lv_label_set_text(detailMessage_, "Looper controls remain hidden until this Spark device reports verified support.");
        break;
    case Screen::Tuner:
        lv_label_set_text(detailTitle_, "TUNER");
        lv_label_set_text(detailMessage_, "Tuner entry is capability-gated until its controller action is wired.");
        break;
    case Screen::Device:
        lv_label_set_text(detailTitle_, "DEVICE");
        lv_label_set_text_fmt(detailMessage_, "%s\n%s\nSpark remains the source of truth.",
                              snapshot.identityKnown ? snapshot.ampName.c_str() : "Reading Spark device...",
                              snapshot.connectionPhase == ControllerConnectionPhase::Ready ? "CONNECTED" : "CONNECTING");
        break;
    case Screen::Preset:
    case Screen::Fx:
        break;
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
    if (activeScreen_ == Screen::Preset || activeScreen_ == Screen::Fx) {
        lv_label_set_text(headerTitle_, snapshot.identityKnown ? snapshot.ampName.c_str() : "IGNITRON");
    }
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
