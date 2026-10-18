#include "lvgl.h"
#include "PresetNamePresentation.h"
#include "position.h"
#include <cassert>
#include <cstdio>

int main() {
    static_assert(LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR == 3 && LVGL_VERSION_PATCH == 0,
                  "Regression must use firmware's pinned LVGL");
    lv_init();
    auto *display = lv_display_create(320, 240);
    static unsigned char buffer[320 * 24 * 2];
    lv_display_set_buffers(display, buffer, nullptr, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, [](lv_display_t *d, const lv_area_t *, uint8_t *) { lv_display_flush_ready(d); });
    auto *screen = lv_screen_active();
    lv_obj_set_style_pad_all(screen, 0, 0);
    auto *hero = lv_button_create(screen);
    lv_obj_set_size(hero, 308, 158);
    lv_obj_align(hero, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_set_style_border_width(hero, 1, 0);
    lv_obj_set_style_pad_all(hero, 0, 0);
    lv_obj_set_style_shadow_width(hero, 0, 0);
    lv_obj_remove_flag(hero, LV_OBJ_FLAG_SCROLLABLE);
    auto *label = lv_label_create(hero);
    lv_obj_set_size(label, 282, 2 * lv_font_get_line_height(&lv_font_montserrat_24));
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_line_space(label, 0, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 42);
    lv_obj_update_layout(screen);
    lv_refr_now(display);
    auto measure = [&](const char *text) {
        lv_point_t size;
        lv_text_get_size(&size, text, lv_obj_get_style_text_font(label, LV_PART_MAIN),
                         lv_obj_get_style_text_letter_space(label, LV_PART_MAIN),
                         0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        return size.x;
    };
    auto *description = lv_label_create(hero);
    lv_obj_set_size(description, 274, 15);
    lv_label_set_long_mode(description, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(description, &lv_font_montserrat_12, 0);
    lv_obj_align(description, LV_ALIGN_TOP_MID, 0, 84);
    auto *fx = lv_obj_create(screen);
    lv_obj_set_size(fx, 47, 47);
    lv_obj_align(fx, LV_ALIGN_TOP_MID, -127, 143);
    assert(measure("Solo Guitar Style Tone 1") == 280);
    assert(lv_obj_get_style_layout(hero, 0) == 0);
    assert(lv_font_get_line_height(&lv_font_montserrat_24) == 27);
    // Control experiment: upstream align does honor a changed Y even when
    // TOP_MID is unchanged. The border adds one pixel to absolute coordinates.
    lv_label_set_text(label, "Solo Guitar Style Tone 1");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_update_layout(screen);
    lv_refr_now(display);
    lv_area_t control;
    lv_obj_get_coords(label, &control);
    assert(lv_obj_get_y(label) == 56 && control.y1 == 91);
    // One -> two -> one, repeated Pending/Loading/Ready-style presentations,
    // and a hidden-parent update. Every assertion follows real LVGL refresh.
    for (const char *name : {"Solo Guitar Style Tone 1", "American Classic Blues 2",
                             "Solo Guitar Style Tone 1", "New"}) {
        auto fitted = PresetNamePresentation::fit(name, 282, measure);
        if (std::string(name) == "Solo Guitar Style Tone 1") {
            assert(fitted.text == name && !fitted.twoLine);
        }
        if (std::string(name) == "American Classic Blues 2") assert(fitted.twoLine);
        for (int phase = 0; phase < 3; ++phase) {
            if (phase == 1) lv_obj_add_flag(hero, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(label, fitted.text.c_str());
            applyPosition(label, fitted);
            if (phase == 1) {
                // Process the update while the parent is actually hidden, then
                // verify the production position after it becomes visible again.
                assert(lv_obj_has_flag(hero, LV_OBJ_FLAG_HIDDEN));
                lv_obj_update_layout(screen);
                lv_refr_now(display);
            }
            lv_obj_remove_flag(hero, LV_OBJ_FLAG_HIDDEN);
            lv_obj_update_layout(screen);
            lv_refr_now(display);
            lv_area_t coords, heroCoords, descCoords, fxCoords;
            lv_obj_get_coords(label, &coords);
            lv_obj_get_coords(hero, &heroCoords);
            lv_obj_get_coords(description, &descCoords);
            lv_obj_get_coords(fx, &fxCoords);
            assert(lv_obj_get_y(label) == (fitted.twoLine ? 42 : 55));
            assert(coords.y1 == (fitted.twoLine ? 77 : 90));
            assert(coords.y1 - heroCoords.y1 == (fitted.twoLine ? 43 : 56));
            assert(coords.x1 == 19 && coords.x2 == 300);
            if (fitted.twoLine) {
                lv_point_t secondLine;
                lv_label_get_letter_pos(label, fitted.text.find('\n') + 1, &secondLine);
                assert(secondLine.y == 27); // Original y42/y69 content anchors.
            }
            assert(descCoords.y1 == 119 && lv_obj_get_height(description) == 15);
            assert(fxCoords.y1 == 143);
            assert(!applyPosition(label, fitted)); // Unchanged position is a no-op.
            std::printf("formatted=[%s] twoLine=%d object_y=%d screen_y=%d\n",
                        fitted.text.c_str(), fitted.twoLine, int(lv_obj_get_y(label)), int(coords.y1));
        }
        // A stale position must be repaired even when the line mode is unchanged.
        lv_obj_set_y(label, 0);
        assert(applyPosition(label, fitted));
        lv_obj_update_layout(screen);
        lv_refr_now(display);
        assert(lv_obj_get_y(label) == (fitted.twoLine ? 42 : 55));
    }
    lv_deinit();
}
