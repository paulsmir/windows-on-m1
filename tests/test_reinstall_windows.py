from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "reinstall-windows.cmd"


class ReinstallWindowsContractTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(SCRIPT.exists(), "production reinstall script is missing")
        self.text = SCRIPT.read_text(encoding="ascii")
        self.lower = self.text.lower()

    def test_discovers_source_and_exact_target_labels(self):
        self.assertIn(r"\sources\install.wim", self.lower)
        self.assertIn(' is windows$"', self.lower)
        self.assertIn(' is winesp$"', self.lower)
        self.assertIn("source_count", self.lower)
        self.assertIn("windows_count", self.lower)
        self.assertIn("winesp_count", self.lower)

    def test_exact_label_patterns_reject_lookalikes(self):
        windows = re.compile(r" is Windows$")
        winesp = re.compile(r" is WINESP$")
        self.assertTrue(windows.search("Volume in drive D is Windows"))
        self.assertTrue(winesp.search("Volume in drive S is WINESP"))
        self.assertFalse(windows.search("Volume in drive E is WindowsARM"))
        self.assertFalse(winesp.search("Volume in drive F is WINESP-backup"))

    def test_requires_literal_confirmation_before_format(self):
        confirmation = self.lower.index("erase windows")
        first_format = self.lower.index("format fs=")
        self.assertLess(confirmation, first_format)

    def test_forbids_partition_table_mutation(self):
        for forbidden in (
            "clean",
            "delete partition",
            "create partition",
            "convert gpt",
            "select disk",
        ):
            self.assertNotRegex(self.lower, rf"(?m)^\s*{re.escape(forbidden)}\b")

    def test_has_no_hard_coded_drive_assignments(self):
        self.assertNotRegex(
            self.lower,
            r'(?im)^set "(?:source|windows|winesp)_drive=[a-z]:"',
        )

    def test_verifies_required_boot_artifacts(self):
        self.assertIn(r"\windows\system32\winload.efi", self.lower)
        self.assertIn(r"\efi\microsoft\boot\bcd", self.lower)
        self.assertIn(r"\efi\boot\bootaa64.efi", self.lower)


if __name__ == "__main__":
    unittest.main()
