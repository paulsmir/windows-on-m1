import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
FV_MAIN = (
    ROOT
    / "mu"
    / "Build"
    / "MacBookAirMid2020-AARCH64"
    / "DEBUG_CLANGPDB"
    / "FV"
    / "FVMAIN.inf"
)


@unittest.skipUnless(FV_MAIN.is_file(), "Project Mu firmware has not been built")
class TestMuFirmwareInterruptBackend(unittest.TestCase):
    def test_j313_guest_firmware_contains_vgic_not_physical_aic_driver(self):
        firmware_manifest = FV_MAIN.read_text()

        self.assertIn("ArmGicDxe", firmware_manifest)
        self.assertNotIn("AppleAicDxe", firmware_manifest)


if __name__ == "__main__":
    unittest.main()
