"""Mu PEI reservation of iBoot carveouts inside guest RAM (HVS2ISO004 candidate).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
  * the GPU-line OS memory map changing anywhere except the planned carveout pages;
  * reservations that differ from m1n1's stage-2 unmap set (stage2_policy.in_ram_carveouts),
    which would let Windows allocate a page the hypervisor has removed;
  * double reservation or overlap with the framebuffer / RAM-disk window / low backing /
    AGX local reserve, and silent no-ops of ReserveMemoryRegion().
The fixture compiles the real ReserveMemoryRegion, AgxLocalReserveHobContains and
ReserveRamCarveouts bodies extracted from MemoryInitPeiLib.c.

Mu tree: $MU_TREE, else ./mu.  Skipped when that tree lacks the change (the parent's mu
gitlink stays at 361a2134 until the change is adopted).
"""
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MU = Path(os.environ.get("MU_TREE", ROOT / "mu"))
HEADER_DIR = MU / "Silicon/Apple/T810XFamilyPkg/Include/Library"
PEI = MU / "Silicon/Apple/T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c"
TEST = ROOT / "tests/fixtures/mu_ram_carveout_test.c"
BASE_H = ("#include <stdint.h>\n#include <stddef.h>\n"
          "typedef uint64_t UINT64; typedef size_t UINTN; typedef int BOOLEAN;\n"
          "#define TRUE 1\n#define FALSE 0\n#define IN\n#define OUT\n#define CONST const\n")


def extract(source, signature):
    start = source.index(signature)
    end = source.index("\n}\n", start) + 3
    return source[start:end]


@unittest.skipUnless((HEADER_DIR / "RamCarveoutReservation.h").is_file(),
                     "Mu tree without RamCarveoutReservation.h")
class MuRamCarveoutTest(unittest.TestCase):
    def test_host_replay_of_extracted_pei_code(self):
        source = PEI.read_text()
        bodies = [extract(source, s) for s in (
            "STATIC VOID ReserveMemoryRegion (",
            "STATIC BOOLEAN AgxLocalReserveHobContains (",
            "STATIC VOID\nReserveRamCarveouts (")]
        with tempfile.TemporaryDirectory() as d:
            Path(d, "Base.h").write_text(BASE_H)
            Path(d, "extracted_pei.inc").write_text("\n".join(bodies))
            binary = Path(d, "mu-ram-carveout")
            build = subprocess.run(
                [os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                 "-Wno-unused-function", "-Wno-unused-parameter", "-I", d,
                 "-I", str(HEADER_DIR), str(TEST), "-o", str(binary)],
                capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr + run.stdout)
            self.assertIn("mu ram carveout: ok", run.stdout)

    def test_pei_order_and_exclusions(self):
        s = PEI.read_text()
        peim = s[s.index("EFI_STATUS EFIAPI MemoryPeim("):]
        call = peim.index("ReserveRamCarveouts (CarveoutExcluded")
        # After every existing reservation, before the MMU is configured.
        for earlier in ("ReserveMemoryRegion (FrameBufferBase",
                        "ReserveMemoryRegion (\n        PcdGet64 (PcdPreloadedRamdiskBase)",
                        "PcdGet64 (PcdLowMemoryWindowBackingBase),\n      PcdGet32",
                        "ReserveMemoryRegion (LocalBase, (UINT32)LocalBytes)"):
            self.assertLess(peim.index(earlier), call, earlier)
        self.assertLess(call, peim.index("InitMmu (MemoryTable)"))
        excl = peim[peim.index("CarveoutExcludedCount = 0;"):call]
        self.assertIn("{UefiMemoryBase, UefiMemorySize, TRUE}", excl)
        self.assertIn("PcdGet32 (PcdFdSize), TRUE}", excl)
        self.assertIn("{FrameBufferBase, FrameBufferSize, FALSE}", excl)
        self.assertIn("if (RamdiskReserved)", excl)
        self.assertIn("{LocalBase, LocalBytes, FALSE}", excl)
        self.assertIn("RamdiskReserved = TRUE;", peim)
        self.assertIn("LocalReserved = TRUE;", peim)
        body = extract(s, "STATIC VOID\nReserveRamCarveouts (")
        self.assertIn('dt_get ("/chosen/carveout-memory-map")', body)
        self.assertIn("BuildMemoryAllocationHob (Plan[Index].Base, Plan[Index].Size, EfiReservedMemoryType)", body)
        self.assertLess(body.index("AgxLocalReserveHobContains (Plan"), body.index("ReserveMemoryRegion (Plan"))
        self.assertNotIn("0x86", body)  # no hard-coded PAs

    def test_merge_gap_matches_m1n1_policy(self):
        header = (HEADER_DIR / "RamCarveoutReservation.h").read_text()
        self.assertIn("#define RAM_CARVEOUT_MERGE_GAP   0x4000ULL", header)
        for tree in (ROOT / "m1n1_windows", ROOT.parent / "m1n1-stage2-above-ram"):
            policy = tree / "proxyclient/m1n1/hv/stage2_policy.py"
            if policy.is_file() and "in_ram_carveouts" in policy.read_text():
                self.assertIn("def in_ram_carveouts(adt, ram_start, ram_end, merge_gap=PAGE_SIZE)",
                              policy.read_text())
                self.assertIn("PAGE_SIZE = 0x4000", policy.read_text())
                return
        self.skipTest("no m1n1 tree with in_ram_carveouts")


if __name__ == "__main__":
    unittest.main()
