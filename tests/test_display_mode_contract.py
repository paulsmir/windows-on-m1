import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "m1n1_windows" / "proxyclient"))

from m1n1.proxy import M1N1Proxy


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

    def test_proxy_forwards_complete_physical_surface_descriptor(self):
        class FakeProxy:
            P_DISPLAY_PREPARE_GUEST_SURFACE = M1N1Proxy.P_DISPLAY_PREPARE_GUEST_SURFACE

            def request(self, *args):
                self.args = args
                return 1

        proxy = FakeProxy()
        result = M1N1Proxy.display_prepare_guest_surface(
            proxy, 0x85F000000, 0xFA0000, 2560, 1600, 10240, 32
        )

        self.assertEqual(result, 1)
        self.assertEqual(
            proxy.args,
            (M1N1Proxy.P_DISPLAY_PREPARE_GUEST_SURFACE,
             0x85F000000, 0xFA0000, 2560, 1600, 10240, 32),
        )

    def test_both_full_enables_both_consumers_and_telemetry(self):
        result = self.run_dry("both", "full")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("physical DCP             : enabled", result.stdout)
        self.assertIn("USB framebuffer          : enabled", result.stdout)
        self.assertIn("telemetry                : enabled", result.stdout)

    def test_monitor_is_accepted_without_full_telemetry(self):
        result = self.run_dry("physical", "monitor")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("debug mode               : monitor", result.stdout)
        self.assertIn("physical DCP             : enabled", result.stdout)
        self.assertIn("USB framebuffer          : disabled", result.stdout)
        self.assertIn("telemetry                : disabled", result.stdout)

    def test_none_off_keeps_gop_without_consumers(self):
        result = self.run_dry("none", "off")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("physical DCP             : disabled", result.stdout)
        self.assertIn("USB framebuffer          : disabled", result.stdout)

    def test_mu_does_not_replace_gop_console_at_ready_to_boot(self):
        source = (
            ROOT
            / "mu"
            / "Silicon"
            / "Apple"
            / "T810XFamilyPkg"
            / "Library"
            / "MsPlatformDevicesLib"
            / "MsPlatformDevicesLib.c"
        ).read_text(encoding="utf-8")

        self.assertNotIn("gST->ConsoleOutHandle = Handle", source)
        self.assertNotIn("gST->ConOut           = TextOut", source)


if __name__ == "__main__":
    unittest.main()
