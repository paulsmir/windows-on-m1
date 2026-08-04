import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class DisplayModeContractTests(unittest.TestCase):
    def run_dry(self, display, debug):
        return subprocess.run(
            [
                sys.executable,
                str(ROOT / "run_uefi.py"),
                "--dry-run",
                "--display-mode",
                display,
                "--debug-mode",
                debug,
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )

    def test_physical_off_disables_usb_observers(self):
        result = self.run_dry("physical", "off")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("display mode             : physical", result.stdout)
        self.assertIn("debug mode               : off", result.stdout)
        self.assertIn("physical DCP             : enabled", result.stdout)
        self.assertIn("USB framebuffer          : disabled", result.stdout)
        self.assertIn("telemetry                : disabled", result.stdout)

    def test_both_full_enables_both_consumers_and_telemetry(self):
        result = self.run_dry("both", "full")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("physical DCP             : enabled", result.stdout)
        self.assertIn("USB framebuffer          : enabled", result.stdout)
        self.assertIn("telemetry                : enabled", result.stdout)

    def test_none_off_keeps_gop_without_consumers(self):
        result = self.run_dry("none", "off")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("physical DCP             : disabled", result.stdout)
        self.assertIn("USB framebuffer          : disabled", result.stdout)


if __name__ == "__main__":
    unittest.main()
