"""Host-only contract check for the read-only controller snapshot status printer."""
from pathlib import Path
import re
import unittest


class FxStatusSourceTest(unittest.TestCase):
    def test_snapshot_fx_status_format(self):
        source = (Path(__file__).resolve().parents[1] / 'src/SparkSerialCLI.cpp').read_text()
        body = source.split('void SparkSerialCLI::printStatus() {', 1)[1].split('\nvoid SparkSerialCLI::printDiagnostics()', 1)[0]
        self.assertRegex(body, r'FX chain: %s\\n')
        self.assertRegex(body, r'FX %s: known=%s model=%s enabled=%s pending=%s failed=%s\\n')
        self.assertIn('snapshot.fxChainIdentity.empty() ? "(unknown)"', body)
        self.assertIn('slot.modelName.empty() ? "(unknown)"', body)
        self.assertIn('"gate", "comp", "drive", "mod", "delay", "reverb"', body)
        self.assertIn('snapshot.fxSlots[i]', body)
        for field in ('known', 'enabled', 'pending', 'actionFailed'):
            self.assertRegex(body, rf'slot\.{field} \? "true" : "false"')
        self.assertLess(body.index('FX chain: %s'), body.index('FX %s: known='))


if __name__ == '__main__':
    unittest.main()
