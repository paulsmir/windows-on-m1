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
        self.assertIn('"!volume_label!"=="windows"', self.lower)
        self.assertIn('"!volume_label!"=="winesp"', self.lower)
        self.assertIn('"volume in drive %%l is"', self.lower)
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

    def test_deploys_image_and_rebuilds_arm64_boot_files(self):
        self.assertGreaterEqual(self.lower.count("select volume"), 2)
        self.assertIn("format fs=ntfs quick label=windows override", self.lower)
        self.assertIn("format fs=fat32 quick label=winesp override", self.lower)
        self.assertIn("dism /apply-image", self.lower)
        self.assertIn("bcdboot", self.lower)
        self.assertIn(r"bootmgfw.efi", self.lower)
        self.assertIn(r"bootaa64.efi", self.lower)

    def test_destructive_operations_follow_confirmation_in_order(self):
        confirmation = self.lower.index('if not "!confirm!"=="erase windows"')
        format_windows = self.lower.index("format fs=ntfs")
        format_esp = self.lower.index("format fs=fat32")
        apply_image = self.lower.index("dism /apply-image")
        bcdboot = self.lower.index("bcdboot")
        verification = self.lower.index(":verify_artifacts")
        success = self.lower.index("result: success")
        positions = [
            confirmation,
            format_windows,
            format_esp,
            apply_image,
            bcdboot,
            verification,
            success,
        ]
        self.assertEqual(positions, sorted(positions))

    def test_checks_failures_and_logs_to_source_volume(self):
        self.assertGreaterEqual(self.lower.count("if errorlevel 1"), 7)
        self.assertIn(r"windows-reinstall.log", self.lower)
        self.assertIn(r"windows-reinstall-dism.log", self.lower)
        self.assertIn("phase:", self.lower)
        self.assertIn("exit code:", self.lower)

    def test_diskpart_files_are_created_only_after_confirmation(self):
        confirmation = self.lower.index('if not "!confirm!"=="erase windows"')
        first_diskpart_file = self.lower.index("reinstall-windows-os.txt")
        self.assertLess(confirmation, first_diskpart_file)

    def test_does_not_depend_on_findstr_in_minimal_winpe(self):
        self.assertNotIn("findstr", self.lower)


if __name__ == "__main__":
    unittest.main()
