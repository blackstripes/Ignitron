#include "PanelLanLVGLUI.h"
#include "controller/ControllerActions.h"

#if defined(PANELAN_SC05X_MODE) && defined(PANELAN_LVGL_UI_MODE)

namespace {
PanelLanLVGLUI *uiInstance = nullptr;
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
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0B1014), 0);
    lv_obj_set_style_pad_all(screen, 14, 0);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "IGNITRON");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *subtitle = lv_label_create(screen);
    lv_label_set_text(subtitle, "SPARK CONTROLLER  /  LVGL + BLE");
    lv_obj_set_style_text_color(subtitle, lv_palette_lighten(LV_PALETTE_BLUE, 2), 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_LEFT, 1, 31);

    lv_obj_t *card = lv_obj_create(screen);
    lv_obj_set_size(card, 292, 86);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, -32);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x18232B), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 10, 0);

    connectionLabel_ = lv_label_create(card);
    lv_obj_align(connectionLabel_, LV_ALIGN_TOP_LEFT, 12, 13);
    identityLabel_ = lv_label_create(card);
    lv_obj_set_width(identityLabel_, 260);
    lv_label_set_long_mode(identityLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(identityLabel_, lv_palette_lighten(LV_PALETTE_GREY, 2), 0);
    lv_obj_align(identityLabel_, LV_ALIGN_TOP_LEFT, 12, 41);

    for (uint8_t preset = 1; preset <= 4; ++preset) {
        lv_obj_t *button = lv_button_create(screen);
        presetButtons_[preset - 1] = button;
        lv_obj_set_size(button, 64, 52);
        lv_obj_align(button, LV_ALIGN_CENTER, -114 + (preset - 1) * 76, 49);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x28353D), 0);
        lv_obj_set_style_border_width(button, 1, 0);
        lv_obj_add_event_cb(button, onPresetClicked, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<uintptr_t>(preset)));
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text_fmt(label, "%u", preset);
        lv_obj_center(label);
    }

    lv_obj_t *footer = lv_label_create(screen);
    lv_label_set_text(footer, "Hardware presets: pending/confirmed checkpoint");
    lv_obj_set_style_text_color(footer, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -7);
}

void PanelLanLVGLUI::onPresetClicked(lv_event_t *event) {
    if (!uiInstance->actions_) {
        return;
    }
    const uint8_t preset = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
    uiInstance->actions_->requestHardwarePreset(preset);
}

void PanelLanLVGLUI::renderStatus(const ControllerSnapshot &snapshot) {
    const bool linkEstablished = snapshot.connectionPhase == ControllerConnectionPhase::Identifying ||
                                 snapshot.connectionPhase == ControllerConnectionPhase::Syncing ||
                                 snapshot.connectionPhase == ControllerConnectionPhase::Ready;
    const lv_color_t accent = linkEstablished ? lv_palette_main(LV_PALETTE_GREEN)
                                               : lv_palette_main(LV_PALETTE_ORANGE);
    const char *connectionText = snapshot.connectionPhase == ControllerConnectionPhase::Reconnecting
                                     ? "RECONNECTING TO SPARK"
                                     : linkEstablished ? "CONNECTED - SYNCING" : "SEARCHING FOR SPARK";
    lv_label_set_text(connectionLabel_, connectionText);
    lv_obj_set_style_text_color(connectionLabel_, accent, 0);
    lv_obj_set_style_border_color(lv_obj_get_parent(connectionLabel_), accent, 0);
    if (snapshot.identityKnown) {
        lv_label_set_text_fmt(identityLabel_, "%s%s%s", snapshot.ampName.c_str(),
                              snapshot.ampSerial.empty() ? "" : "\nSerial  ",
                              snapshot.ampSerial.c_str());
    } else if (linkEstablished) {
        lv_label_set_text(identityLabel_, "Reading Spark identity and current state...");
    } else {
        lv_label_set_text(identityLabel_, "Spark-owned controls remain disabled\nuntil a fresh connection is ready.");
    }

    for (uint8_t preset = 1; preset <= 4; ++preset) {
        lv_obj_t *button = presetButtons_[preset - 1];
        const bool pending = snapshot.pendingHardwarePreset == preset;
        const bool confirmed = snapshot.confirmedHardwarePreset == preset;
        const bool enabled = snapshot.connectionPhase == ControllerConnectionPhase::Ready &&
                             snapshot.pendingHardwarePreset == 0 && !confirmed;
        const lv_color_t color = pending ? lv_palette_main(LV_PALETTE_ORANGE)
                               : confirmed ? lv_palette_main(LV_PALETTE_GREEN)
                               : enabled ? lv_palette_main(LV_PALETTE_BLUE)
                                         : lv_color_hex(0x28353D);
        lv_obj_set_style_bg_color(button, color, 0);
        if (enabled) {
            lv_obj_remove_state(button, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(button, LV_STATE_DISABLED);
        }
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
