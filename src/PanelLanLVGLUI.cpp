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
    lv_label_set_text(title, "IGNITRON");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 10, 0);

    connectionLabel_ = lv_label_create(header);
    lv_obj_set_width(connectionLabel_, 140);
    lv_obj_set_style_text_align(connectionLabel_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(connectionLabel_, LV_ALIGN_RIGHT_MID, -10, 0);

    lv_obj_t *hero = lv_obj_create(screen);
    lv_obj_set_size(hero, 300, 66);
    lv_obj_align(hero, LV_ALIGN_TOP_MID, 0, 38);
    lv_obj_set_style_bg_color(hero, lv_color_hex(0x111B21), 0);
    lv_obj_set_style_border_color(hero, lv_color_hex(0x2A3B45), 0);
    lv_obj_set_style_border_width(hero, 1, 0);
    lv_obj_set_style_radius(hero, 7, 0);
    lv_obj_set_style_pad_all(hero, 0, 0);

    identityLabel_ = lv_label_create(hero);
    lv_obj_set_width(identityLabel_, 270);
    lv_obj_set_style_text_color(identityLabel_, lv_palette_lighten(LV_PALETTE_GREY, 2), 0);
    lv_obj_align(identityLabel_, LV_ALIGN_TOP_LEFT, 12, 8);

    presetNameLabel_ = lv_label_create(hero);
    lv_obj_set_width(presetNameLabel_, 270);
    lv_label_set_long_mode(presetNameLabel_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(presetNameLabel_, lv_color_white(), 0);
    lv_obj_set_style_text_font(presetNameLabel_, &lv_font_montserrat_20, 0);
    lv_obj_align(presetNameLabel_, LV_ALIGN_TOP_LEFT, 11, 24);

    presetMetaLabel_ = lv_label_create(hero);
    lv_obj_set_style_text_color(presetMetaLabel_, lv_palette_lighten(LV_PALETTE_BLUE, 2), 0);
    lv_obj_align(presetMetaLabel_, LV_ALIGN_BOTTOM_RIGHT, -12, -7);

    lv_obj_t *sectionLabel = lv_label_create(screen);
    lv_label_set_text(sectionLabel, "HARDWARE PRESETS");
    lv_obj_set_style_text_color(sectionLabel, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
    lv_obj_align(sectionLabel, LV_ALIGN_TOP_LEFT, 11, 111);

    for (uint8_t preset = 1; preset <= 4; ++preset) {
        lv_obj_t *button = lv_button_create(screen);
        presetButtons_[preset - 1] = button;
        const int x = preset % 2 == 1 ? -76 : 76;
        const int y = preset <= 2 ? 22 : 68;
        lv_obj_set_size(button, 140, 38);
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

    lv_obj_t *nav = lv_obj_create(screen);
    lv_obj_set_size(nav, 320, 27);
    lv_obj_align(nav, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(nav, lv_color_hex(0x111B21), 0);
    lv_obj_set_style_border_width(nav, 0, 0);
    lv_obj_set_style_radius(nav, 0, 0);
    lv_obj_set_style_pad_all(nav, 0, 0);
    const char *navLabels[] = {"PRESET", "FX", "LOOPER", "TUNER", "DEVICE"};
    for (uint8_t i = 0; i < 5; ++i) {
        lv_obj_t *label = lv_label_create(nav);
        lv_label_set_text(label, navLabels[i]);
        lv_obj_set_width(label, 64);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(label, i == 0 ? lv_palette_main(LV_PALETTE_YELLOW)
                                                   : lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, i * 64, 0);
    }
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
                                     ? "RECONNECTING"
                                     : snapshot.connectionPhase == ControllerConnectionPhase::Ready ? "● CONNECTED"
                                     : linkEstablished ? "● SYNCING" : "SEARCHING";
    lv_label_set_text(connectionLabel_, connectionText);
    lv_obj_set_style_text_color(connectionLabel_, accent, 0);
    lv_obj_set_style_border_color(lv_obj_get_parent(connectionLabel_), accent, 0);
    if (snapshot.identityKnown) {
        lv_label_set_text(identityLabel_, snapshot.ampName.c_str());
    } else if (linkEstablished) {
        lv_label_set_text(identityLabel_, "Reading Spark state...");
    } else {
        lv_label_set_text(identityLabel_, "Turn on your Spark device");
    }

    lv_label_set_text(presetNameLabel_, snapshot.presetName.empty() ? "WAITING FOR PRESET" : snapshot.presetName.c_str());
    if (snapshot.confirmedHardwarePreset != 0) {
        lv_label_set_text_fmt(presetMetaLabel_, "HW %u", snapshot.confirmedHardwarePreset);
    } else {
        lv_label_set_text(presetMetaLabel_, "SYNCING");
    }

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

    if (snapshot.pendingHardwarePreset != 0) {
        lv_label_set_text(actionStatusLabel_, "Switching preset...");
    } else if (snapshot.presetActionFailed) {
        lv_label_set_text(actionStatusLabel_, "Preset switch was not confirmed");
    } else {
        lv_label_set_text(actionStatusLabel_, "Hardware presets ready");
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
