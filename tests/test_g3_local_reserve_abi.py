from pathlib import Path
import re
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[1]
M1N1 = ROOT / "m1n1_windows/src/hv_agx_local_reserve.h"
MU = ROOT / "mu/Silicon/Apple/T810XFamilyPkg/Include/Library/AgxLocalReserveValidation.h"
KMD = ROOT / "drivers/apple-agx/shared/include/apple_agx_local_reserve_abi.h"


def define(path, name):
    source = subprocess.check_output(["cc", "-E", "-dM", "-x", "c", "-DAGX_LOCAL_RESERVE_V2", str(path)], text=True) if path == M1N1 else path.read_text()
    line = next(line for line in source.splitlines()
                if line.startswith(f"#define {name} "))
    value = line.split(name, 1)[1]
    hexa = re.search(r"0x[0-9a-fA-F]+", value)
    return int(hexa.group(), 16) if hexa else int(re.findall(r"[0-9]+", value)[-1])


class LocalReserveAbiTest(unittest.TestCase):
    def test_all_layers_agree_on_version_magic_size_and_broker_window(self):
        self.assertEqual(define(M1N1, "HV_AGX_LOCAL_ABI_MAGIC"),
                         define(KMD, "APPLE_AGX_LOCAL_RESERVE_MAGIC"))
        self.assertEqual(define(MU, "AGX_LOCAL_BROKER_MAGIC"),
                         define(KMD, "APPLE_AGX_LOCAL_RESERVE_MAGIC"))
        self.assertEqual(define(M1N1, "HV_AGX_LOCAL_ABI_VERSION"),
                         define(KMD, "APPLE_AGX_LOCAL_RESERVE_VERSION"))
        self.assertEqual(define(MU, "AGX_LOCAL_BROKER_VERSION"),
                         define(KMD, "APPLE_AGX_LOCAL_RESERVE_VERSION"))
        self.assertEqual(define(M1N1, "HV_AGX_LOCAL_BYTES"),
                         define(MU, "AGX_LOCAL_RESERVE_BYTES"))
        self.assertEqual(define(MU, "AGX_LOCAL_RESERVE_BYTES"),
                         define(KMD, "APPLE_AGX_LOCAL_RESERVE_BYTES"))
        self.assertEqual(define(M1N1, "HV_AGX_LOCAL_MMIO_OFFSET"),
                         define(KMD, "APPLE_AGX_LOCAL_RESERVE_OFFSET"))
        self.assertEqual(define(MU, "AGX_LOCAL_BROKER_BASE"),
                         0x300000000 + define(KMD, "APPLE_AGX_LOCAL_RESERVE_OFFSET"))


if __name__ == "__main__":
    unittest.main()
