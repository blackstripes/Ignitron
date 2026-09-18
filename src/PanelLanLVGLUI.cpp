#include "PanelLanLVGLUI.h"
#include "controller/ControllerActions.h"

#include <esp_heap_caps.h>

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
    lv_label_set_text(title, "IGNITRON");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 10, 0);

    connectionLabel_ = lv_label_create(header);
    lv_obj_set_width(connectionLabel_, 140);
    lv_obj_set_style_text_align(connectionLabel_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(connectionLabel_, LV_ALIGN_RIGHT_MID, -10, 0);

    lv_obj_t *hero = lv_button_create(screen);
    lv_obj_set_size(hero, 300, 96);
    lv_obj_align(hero, LV_ALIGN_TOP_MID, 0, 37);
    lv_obj_set_style_bg_color(hero, lv_color_hex(0x16232B), 0);
    lv_obj_set_style_bg_grad_color(hero, lv_color_hex(0x0A1116), 0);
    lv_obj_set_style_bg_grad_dir(hero, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_border_color(hero, lv_color_hex(0x3B515D), 0);
    lv_obj_set_style_border_width(hero, 1, 0);
    lv_obj_set_style_radius(hero, 7, 0);
    lv_obj_set_style_pad_all(hero, 0, 0);
    lv_obj_add_event_cb(hero, onPresetCardClicked, LV_EVENT_CLICKED, nullptr);

    identityLabel_ = lv_label_create(hero);
    lv_obj_set_width(identityLabel_, 270);
    lv_obj_set_style_text_color(identityLabel_, lv_palette_lighten(LV_PALETTE_GREY, 2), 0);
    lv_obj_align(identityLabel_, LV_ALIGN_TOP_LEFT, 13, 9);

    presetNameLabel_ = lv_label_create(hero);
    lv_obj_set_width(presetNameLabel_, 270);
    lv_label_set_long_mode(presetNameLabel_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(presetNameLabel_, lv_color_white(), 0);
    lv_obj_set_style_text_font(presetNameLabel_, &lv_font_montserrat_20, 0);
    lv_obj_align(presetNameLabel_, LV_ALIGN_TOP_LEFT, 12, 30);

    presetDescriptionLabel_ = lv_label_create(hero);
    lv_obj_set_width(presetDescriptionLabel_, 230);
    lv_label_set_long_mode(presetDescriptionLabel_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(presetDescriptionLabel_, lv_palette_lighten(LV_PALETTE_GREY, 2), 0);
    lv_obj_align(presetDescriptionLabel_, LV_ALIGN_TOP_LEFT, 14, 61);

    presetMetaLabel_ = lv_label_create(hero);
    lv_obj_set_style_text_color(presetMetaLabel_, lv_palette_lighten(LV_PALETTE_BLUE, 2), 0);
    lv_obj_align(presetMetaLabel_, LV_ALIGN_BOTTOM_RIGHT, -13, -10);

    lv_obj_t *sectionLabel = lv_label_create(screen);
    lv_label_set_text(sectionLabel, "ACTIVE EFFECTS");
    lv_obj_set_style_text_color(sectionLabel, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
    lv_obj_align(sectionLabel, LV_ALIGN_TOP_LEFT, 12, 139);

    const int16_t fxX[] = {-132, -79, -26, 27, 80, 133};
    for (uint8_t fx = 0; fx < 6; ++fx) {
        lv_obj_t *tile = lv_obj_create(screen);
        fxTiles_[fx] = tile;
        lv_obj_set_size(tile, 48, 42);
        lv_obj_align(tile, LV_ALIGN_TOP_MID, fxX[fx], 157);
        lv_obj_set_style_bg_color(tile, lv_color_hex(0x151F25), 0);
        lv_obj_set_style_border_color(tile, lv_color_hex(0x34454F), 0);
        lv_obj_set_style_border_width(tile, 1, 0);
        lv_obj_set_style_radius(tile, 5, 0);
        lv_obj_set_style_pad_all(tile, 0, 0);
        lv_obj_t *label = lv_label_create(tile);
        lv_label_set_text(label, "FX");
        lv_obj_set_width(label, 48);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 6);
        fxStateLabels_[fx] = lv_label_create(tile);
        lv_label_set_text(fxStateLabels_[fx], "--");
        lv_obj_set_width(fxStateLabels_[fx], 48);
        lv_obj_set_style_text_align(fxStateLabels_[fx], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(fxStateLabels_[fx], LV_ALIGN_BOTTOM_MID, 0, -5);
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
    lv_obj_add_flag(detailPage_, LV_OBJ_FLAG_HIDDEN);

    detailTitle_ = lv_label_create(detailPage_);
    lv_obj_set_style_text_color(detailTitle_, lv_color_white(), 0);
    lv_obj_set_style_text_font(detailTitle_, &lv_font_montserrat_20, 0);
    lv_obj_align(detailTitle_, LV_ALIGN_TOP_LEFT, 12, 10);

    detailMessage_ = lv_label_create(detailPage_);
    lv_obj_set_width(detailMessage_, 292);
    lv_label_set_long_mode(detailMessage_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(detailMessage_, lv_palette_lighten(LV_PALETTE_GREY, 2), 0);
    lv_obj_align(detailMessage_, LV_ALIGN_TOP_LEFT, 14, 43);

    const int16_t detailX[] = {-105, 0, 105, -105, 0, 105};
    for (uint8_t fx = 0; fx < 6; ++fx) {
        lv_obj_t *tile = lv_obj_create(detailPage_);
        detailTiles_[fx] = tile;
        lv_obj_set_size(tile, 94, 48);
        lv_obj_align(tile, LV_ALIGN_TOP_MID, detailX[fx], fx < 3 ? 72 : 126);
        lv_obj_set_style_border_width(tile, 1, 0);
        lv_obj_set_style_radius(tile, 6, 0);
        lv_obj_set_style_pad_all(tile, 0, 0);
        detailTileLabels_[fx] = lv_label_create(tile);
        lv_obj_set_width(detailTileLabels_[fx], 94);
        lv_obj_set_style_text_align(detailTileLabels_[fx], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(detailTileLabels_[fx], lv_color_white(), 0);
        lv_obj_center(detailTileLabels_[fx]);
    }

    lv_obj_t *nav = lv_obj_create(screen);
    lv_obj_set_size(nav, 320, 30);
    lv_obj_align(nav, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(nav, lv_color_hex(0x111B21), 0);
    lv_obj_set_style_border_width(nav, 0, 0);
    lv_obj_set_style_radius(nav, 0, 0);
    lv_obj_set_style_pad_all(nav, 0, 0);
    const char *navLabels[] = {"PRESET", "FX", "LOOPER", "TUNER", "DEVICE"};
    for (uint8_t i = 0; i < 5; ++i) {
        lv_obj_t *button = lv_button_create(nav);
        navButtons_[i] = button;
        lv_obj_set_size(button, 64, 30);
        lv_obj_align(button, LV_ALIGN_LEFT_MID, i * 64, 0);
        lv_obj_set_style_pad_all(button, 0, 0);
        lv_obj_set_style_radius(button, 4, 0);
        lv_obj_add_event_cb(button, onNavClicked, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
        lv_obj_t *label = lv_label_create(button);
        navLabels_[i] = label;
        lv_label_set_text(label, navLabels[i]);
        lv_obj_set_width(label, 64);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(label);
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
    for (uint8_t i = 0; i < 5; ++i) {
        const bool selected = i == static_cast<uint8_t>(activeScreen_);
        lv_obj_set_style_bg_color(navButtons_[i], selected ? lv_color_hex(0x1E2020) : lv_color_hex(0x111B21), 0);
        lv_obj_set_style_border_color(navButtons_[i], selected ? lv_palette_main(LV_PALETTE_YELLOW)
                                                               : lv_color_hex(0x111B21), 0);
        lv_obj_set_style_border_width(navButtons_[i], selected ? 1 : 0, 0);
        lv_obj_set_style_text_color(navLabels_[i], selected ? lv_palette_main(LV_PALETTE_YELLOW)
                                                            : lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
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
        lv_label_set_text(detailTitle_, "FX CONTROL");
        lv_label_set_text(detailMessage_, "LIVE SPARK EFFECT STATE");
        for (uint8_t fx = 0; fx < 6; ++fx) {
            const ControllerFxSlot &slot = snapshot.fxSlots[fx];
            const char *name = slot.known && slot.label == "REVERB" ? "REV" : slot.known ? slot.label.c_str() : "--";
            lv_label_set_text_fmt(detailTileLabels_[fx], "%s\n%s", name,
                                  slot.known ? (slot.enabled ? "ON" : "OFF") : "SYNC");
            const lv_color_t color = slot.enabled ? lv_color_hex(0x176B48) : lv_color_hex(0x151F25);
            lv_obj_set_style_bg_color(detailTiles_[fx], color, 0);
            lv_obj_set_style_border_color(detailTiles_[fx], slot.enabled ? lv_color_hex(0x2CB67D)
                                                                         : lv_color_hex(0x34454F), 0);
        }
        return;
    }
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

    if (!snapshot.presetName.empty()) {
        lv_label_set_text(presetNameLabel_, snapshot.presetName.c_str());
    } else if (snapshot.confirmedHardwarePreset != 0) {
        lv_label_set_text_fmt(presetNameLabel_, "PRESET %u", snapshot.confirmedHardwarePreset);
    } else {
        lv_label_set_text(presetNameLabel_, "SYNCING PRESET...");
    }
    const bool hasUsefulDescription = !snapshot.presetDescription.empty() && snapshot.presetDescription != "Text";
    lv_label_set_text(presetDescriptionLabel_, hasUsefulDescription
                                                ? snapshot.presetDescription.c_str()
                                                : "Tap card to choose a hardware preset");
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

    static const lv_color_t kFxColors[] = {
        lv_color_hex(0x16A34A), lv_color_hex(0x64748B), lv_color_hex(0xDC2626),
        lv_color_hex(0x64748B), lv_color_hex(0x0284C7), lv_color_hex(0x64748B),
    };
    for (uint8_t fx = 0; fx < 6; ++fx) {
        const ControllerFxSlot &slot = snapshot.fxSlots[fx];
        lv_obj_t *tile = fxTiles_[fx];
        lv_obj_t *label = lv_obj_get_child(tile, 0);
        const char *labelText = slot.known && slot.label == "REVERB" ? "REV"
                              : slot.known ? slot.label.c_str() : "--";
        lv_label_set_text(label, labelText);
        lv_label_set_text(fxStateLabels_[fx], slot.known ? (slot.enabled ? "ON" : "OFF") : "--");
        const lv_color_t color = slot.enabled ? kFxColors[fx] : lv_color_hex(0x151F25);
        lv_obj_set_style_bg_color(tile, color, 0);
        lv_obj_set_style_border_color(tile, slot.enabled ? kFxColors[fx] : lv_color_hex(0x34454F), 0);
        lv_obj_set_style_text_color(fxStateLabels_[fx], slot.enabled ? lv_color_white()
                                                                      : lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
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
