from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
HEADER_DIR = ROOT / "mu/Silicon/Apple/T810XFamilyPkg/Include/Library"
TEST = ROOT / "tests/fixtures/agx_mu_local_reserve_test.c"
PEI = ROOT / "mu/Silicon/Apple/T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c"


class MuLocalReserveTest(unittest.TestCase):
    def test_pei_reserves_before_dxe_and_maps_broker_as_device(self):
        source = PEI.read_text()
        self.assertIn("AgxLocalReserveRangeValid", source)
        self.assertIn("BuildMemoryAllocationHob (LocalBase, AGX_LOCAL_RESERVE_BYTES, EfiReservedMemoryType)", source)
        self.assertLess(source.index("ReserveMemoryRegion (LocalBase"),
                        source.index("BuildMemoryAllocationHob (LocalBase"))
        self.assertLess(source.index("BuildMemoryAllocationHob (LocalBase"),
                        source.index("InitMmu (MemoryTable)"))
        self.assertIn("AGX_LOCAL_BROKER_BASE", source)

    def test_actual_pei_validator_rejects_overlap_and_overflow(self):
        with tempfile.TemporaryDirectory() as directory:
            stub = Path(directory) / "Base.h"
            stub.write_text("#include <stdint.h>\n#include <stddef.h>\n"
                            "typedef uint64_t UINT64; typedef size_t UINTN; "
                            "typedef int BOOLEAN;\n#define TRUE 1\n#define FALSE 0\n"
                            "#define IN\n#define CONST const\n")
            binary = Path(directory) / "mu-validator"
            build = subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall",
                                    "-Wextra", "-Werror", "-I", directory,
                                    "-I", str(HEADER_DIR), str(TEST), "-o", str(binary)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
