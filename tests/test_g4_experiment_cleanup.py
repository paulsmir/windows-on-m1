"""Guard the exact installed-package rollback used after GPU experiments."""

from pathlib import Path
import unittest


SCRIPT = (Path(__file__).resolve().parents[1] /
          "drivers/apple-agx/windows/scripts/remove-experiment-driver.ps1")


class ExperimentCleanupContract(unittest.TestCase):
    def test_remove_devnode_before_exact_package_uninstall(self):
        self.assertTrue(SCRIPT.exists(), "exact experiment cleanup script is missing")
        source = SCRIPT.read_text()
        self.assertIn("ExpectedPublishedName", source)
        self.assertIn("ExpectedInfSha256", source)
        self.assertIn("ExpectedSysSha256", source)
        self.assertIn("G3Armed", source)
        self.assertLess(source.index("/remove-device"), source.index("/delete-driver"))
        self.assertIn("/uninstall", source)
        self.assertIn("staged package remains", source)
        self.assertIn("phantom devnode remains", source)
        self.assertIn("$remaining = Get-PnpDevice", source)
        self.assertIn("[int]$remaining.Problem -eq 45", source)
        self.assertIn("/remove-device $id", source)
        self.assertIn("CleanupComplete = $false", source)
        self.assertIn("PendingOrderedGuestRestart", source)
        self.assertLess(source.index("/remove-device"),
                        source.index("service still running"))
        self.assertLess(source.index("service still running"),
                        source.index("/delete-driver"))


if __name__ == "__main__":
    unittest.main()
