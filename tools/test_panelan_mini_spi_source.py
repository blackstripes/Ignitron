"""Host-only contract check for integrated SPI mini display selection and slots."""
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class MiniSpiSourceTest(unittest.TestCase):
    def test_six_sequential_panels_and_caches(self):
        source = (ROOT / 'src/PanelLanMiniDisplaySPI.cpp').read_text()
        header = (ROOT / 'src/PanelLanMiniDisplay.h').read_text()
        self.assertEqual([int(n) for n in re.findall(r'PanelLanMiniSPI tft(\d+)\(0\);', source)],
                         list(range(1, 7)))
        self.assertIn('PanelLanMiniSPI *const tfts[] = {&tft1, &tft2, &tft3, &tft4, &tft5, &tft6};', source)
        self.assertIn('static constexpr uint8_t miniCount_ = 6;', header)
        for field in ('lastView_[miniCount_]', 'lastCard_[miniCount_][96]', 'cardDrawn_[miniCount_]'):
            self.assertIn(field, header)
        self.assertIn('while (miniInitialized_ < miniCount_)', source)
        self.assertIn('const uint8_t display = miniInitialized_ + 1;', source)
        self.assertIn('PanelLanMiniSPI &tft = *tfts[miniInitialized_];', source)
        self.assertIn('if (miniInitialized_ < miniCount_)', source)
        self.assertIn('for (uint8_t i = 0; i < miniCount_; ++i)', source)
        self.assertIn('const uint8_t display = i + 1;', source)
        self.assertIn('miniSelect.select(display)', source)
        self.assertIn('miniSelect.deselect()', source)

    def test_preset_and_fx_use_corresponding_snapshot_slots(self):
        source = (ROOT / 'src/PanelLanMiniDisplaySPI.cpp').read_text()
        self.assertIn('HardwarePresetNames::label(snapshot.hardwarePresetNames, target)', source)
        self.assertIn('snapshot.confirmedHardwarePreset == target', source)
        self.assertIn('PhysicalPresetLayout::positions[i]', source)
        self.assertIn('browsedBank * 4 + position.slot', source)
        self.assertIn('target <= presetCount', source)
        self.assertIn('BANK%c', source)
        self.assertIn('const uint8_t display = i + 1;', source)
        self.assertIn('for (uint8_t i = 0; i < miniCount_; ++i)', source)
        self.assertIn('constexpr const char *fxNames[] = {"GATE", "COMP", "DRIVE", "MOD", "DELAY", "REVERB"};', source)
        self.assertIn('strcpy(card.name, fxNames[i]);', source)
        self.assertIn('const ControllerFxSlot &slot = snapshot.fxSlots[i];', source)
        self.assertIn('constexpr uint16_t fxAccents[] = {gateGreen, compTeal, driveRed, modPurple, delayBlue, reverbOrange};', source)
        self.assertIn('const uint16_t fxAccent = fxAccents[i];', source)
        for field in ('slot.pending', 'slot.actionFailed', '!slot.known', 'slot.enabled'):
            self.assertIn(field, source)

    def test_mcp_display_one_through_six_select_active_low_gpb_zero_through_five(self):
        source = (ROOT / 'src/PanelLanMiniMCP23017.h').read_text()
        self.assertIn('GPB0..5 = active-low TFT1..6 CS', source)
        self.assertIn('static constexpr uint8_t csMask_ = 0x3f;', source)
        self.assertIn('static constexpr uint8_t resetMask_ = 0x40;', source)
        self.assertIn('static constexpr uint8_t high_ = csMask_ | resetMask_;', source)
        self.assertIn('display < 1 || display > 6', source)
        self.assertIn('high_ & ~(1u << (display - 1))', source)
        for display in range(1, 7):
            selected = 0x7f & ~(1 << (display - 1))
            self.assertEqual(selected & 0x3f, 0x3f ^ (1 << (display - 1)))
            self.assertEqual(selected & 0x40, 0x40)
        self.assertIn('miniSelect.select(display)',
                      (ROOT / 'src/PanelLanMiniDisplaySPI.cpp').read_text())


if __name__ == '__main__':
    unittest.main()
