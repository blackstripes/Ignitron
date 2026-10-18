"""Focused guards for the static, pixel-fitted main PRESET card."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
UI = (ROOT / "src/PanelLanLVGLUI.cpp").read_text()
HEADER = (ROOT / "src/PanelLanLVGLUI.h").read_text()


class PresetCosmeticsTest(unittest.TestCase):
    def test_removed_visuals_and_preserved_tap(self):
        for removed in ("identityLabel_", "presetMetaLabel_", "HARDWARE PRESET %u",
                        "Tap to choose a preset"):
            self.assertNotIn(removed, UI + HEADER)
        hero = UI.split("lv_obj_t *hero = lv_button_create(screen);", 1)[1].split("const int16_t fxX[]", 1)[0]
        self.assertNotIn("LV_SYMBOL_RIGHT", hero)
        self.assertIn("onPresetCardClicked, LV_EVENT_CLICKED", hero)
        self.assertIn("lv_obj_remove_flag(presetNameLabel_, LV_OBJ_FLAG_CLICKABLE)", hero)
        self.assertIn('!displayDescription.empty() && displayDescription != "Text"', UI)
        self.assertIn('hasUsefulDescription ? displayDescription.c_str() : ""', UI)

    def test_cabinet_artwork_without_overlay(self):
        cabinet = UI.split("void drawPresetCabinet(", 1)[1].split("void drawGlyph(", 1)[0]
        for removed in ("shade", "overlay", "LV_OPA_60", "GREEN"):
            self.assertNotIn(removed, cabinet)
        for original in (
            "rect(3, 3, 300, 151, 0x111619, 0x354047, 7)",
            "rect(8, 8, 290, 27, 0x11191C, 0x283237, 4)",
            "rect(9, 38, 288, 110, 0x191D1D, 0x2E3537, 4)",
            "line(x, 42, x, 144, 0x292B27)",
            "line(13, y, 293, y, 0x202422)",
        ):
            self.assertIn(original, cabinet)

    def test_fixed_single_line_measured_with_label_style(self):
        self.assertIn("lv_obj_set_size(presetNameLabel_, 282, lv_font_get_line_height(&lv_font_montserrat_24))", UI)
        self.assertIn("lv_label_set_long_mode(presetNameLabel_, LV_LABEL_LONG_CLIP)", UI)
        self.assertIn("lv_obj_set_style_text_align(presetNameLabel_, LV_TEXT_ALIGN_CENTER", UI)
        fitting = UI.split("const auto fittedName =", 1)[1].split("const std::string &displayDescription", 1)[0]
        self.assertIn("PresetNamePresentation::singleLine(name, 282", fitting)
        self.assertIn("lv_text_get_size(&size, text", fitting)
        self.assertIn("lv_obj_get_style_text_font(presetNameLabel_, LV_PART_MAIN)", fitting)
        self.assertIn("lv_obj_get_style_text_letter_space(presetNameLabel_, LV_PART_MAIN)", fitting)
        self.assertIn("LV_COORD_MAX, LV_TEXT_FLAG_NONE", fitting)
        self.assertIn("setText(presetNameLabel_, fittedName.c_str())", fitting)

    def test_static_lifecycle_and_changed_only_updates(self):
        # No asynchronous reveal to restart on Pending -> Ready, or survive
        # a changed preset / departure from Preset. Same fitted text is a no-op.
        self.assertNotIn("lv_anim_", UI)
        self.assertNotIn("lv_timer_create", UI)
        self.assertIn("if (std::strcmp(lv_label_get_text(obj), text) == 0) return false;", UI)
        self.assertIn("showingPreview ? preview.name.c_str() : presetCard.name.c_str()", UI)


if __name__ == "__main__":
    unittest.main()
