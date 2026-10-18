"""Keep PrePi's FV2 parent identity usable by the DXE dispatcher."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
FDF = ROOT / "mu/Platform/MacBookAirMid2020Pkg/MacBookAirMid2020.fdf"
FV_LIB = ROOT / "mu/Common/TIANO/EmbeddedPkg/Library/PrePiLib/FwVol.c"
DISPATCHER = ROOT / "mu/MU_BASECORE/MdeModulePkg/Core/Dxe/Dispatcher/Dispatcher.c"


def volume_name(section: str) -> str | None:
    block = re.search(rf"(?ims)^\[FV\.{section}\]\s*(.*?)(?=^\[FV\.|\Z)", FDF.read_text()).group(1)
    match = re.search(r"(?im)^FvNameGuid\s*=\s*([0-9a-f-]{36})\s*$", block)
    return match.group(1).lower() if match else None


class FvIdentityTest(unittest.TestCase):
    def test_prepi_fv2_parent_has_stable_distinct_name(self):
        self.assertIn("&ParentVolumeInfo.FvName", FV_LIB.read_text())
        self.assertIn("FvFoundInHobFv2 (&KnownHandle->FvNameGuid, &NameGuid)", DISPATCHER.read_text())
        compact = volume_name("FVMAIN_COMPACT")
        main = volume_name("FvMain")
        self.assertIsNotNone(compact, "parent FV2 name must not be uninitialized")
        self.assertIsNotNone(main, "inner FV must have a stable identity")
        self.assertNotEqual(compact, main)


if __name__ == "__main__":
    unittest.main()
