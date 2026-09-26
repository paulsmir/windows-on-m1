"""Offline ordering gate for R106.1's guest reboot and host WDT transition."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "drivers/apple-agx/windows/scripts"


class DurableTransitionContract(unittest.TestCase):
    def test_guest_transition_checks_identity_then_reboots_then_verifies_new_boot(self):
        source = (SCRIPTS / "durable-transition.ps1").read_text()
        self.assertLess(source.index("Assert-ScriptHashes"), source.index("shutdown.exe /r"))
        self.assertLess(source.index("Assert-PackageState"), source.index("shutdown.exe /r"))
        self.assertLess(source.index("BeforeBoot"), source.index("shutdown.exe /r"))
        self.assertIn("LastBootUpTime", source)
        self.assertIn("Flush($true)", source)
        self.assertIn("unexpected live bind after ordered reboot", source)

    def test_package_and_arm_entry_points_require_ordered_transition(self):
        for name in ("stage-driver.ps1", "remove-staged-driver.ps1",
                     "remove-experiment-driver.ps1", "arm-gpuva-experiment.ps1"):
            with self.subTest(name=name):
                source = (SCRIPTS / name).read_text()
                self.assertIn("durable-transition.ps1", source)
                self.assertIn("-Mode Commit", source)


if __name__ == "__main__":
    unittest.main()
