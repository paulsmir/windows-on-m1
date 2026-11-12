"""PITCH-1: the row pitch of a scan-out-capable allocation.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
A window allocation the overlay check or the scanout broker refuses because
of its pitch (a 1682-wide window had pitch 6736, Check3 answered
AdmissionMpoReasonStride and DWM composited it on the GPU), a pitch or size
disagreement between create, open, the Asahi linear import and the broker
record, and any change to a non-displayable allocation's pitch or size.
See investigation/analysis/PITCH-1-aligned-display-pitch.md.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "drivers/apple-agx/render-admission"
SHARED = ROOT / "drivers/apple-agx/shared"


class RenderDisplayPitchTests(unittest.TestCase):
    def test_display_pitch_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "display_pitch_test"
            # -Wconversion/-Wsign-conversion stand in for MSVC C4242/C4244.
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-Wconversion", "-Wsign-conversion", "-pedantic",
                "-fsanitize=address,undefined",
                "-I", str(DRIVER / "include"), "-I", str(SHARED / "include"),
                str(DRIVER / "tests/display_pitch_test.c"),
                str(DRIVER / "src/render_allocation.c"),
                str(DRIVER / "src/mpo_contract.c"),
                str(DRIVER / "src/mpo_layers.c"),
                "-o", str(binary),
            ], cwd=ROOT, check=True)
            run = subprocess.run([str(binary)], cwd=ROOT, capture_output=True,
                                 text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertIn("display_pitch_test: OK", run.stdout)

    def test_scanout_capable_is_inside_the_display_class(self):
        # A ScanoutCapable allocation outside the display class would keep
        # the 16-byte pitch and be refused by Check3 (the 1682 symptom).
        # The kernel predicate is not host-compilable; it must call the
        # allocator's class predicate rather than restate it.
        windows = (DRIVER / "src/allocation_windows.c").read_text()
        scanout = re.search(r"allocation->ScanoutCapable =.*?;", windows,
                            re.S).group()
        self.assertIn("AdmissionAllocationDisplayClass(", scanout)

    def test_create_and_every_open_use_the_one_size_limit(self):
        # PITCH-2: a separate open cap (formerly 16 MiB in umd.c and
        # agx_d3d12_shared_open.h) refused surfaces create had accepted.
        agx = ROOT / "drivers/apple-agx"
        umd = (DRIVER / "umd/src/umd.c").read_text()
        create = re.search(r"VOID AdmissionUmdCreateResourceWithMetadata\([^;]*?\)\s*\{.*?^}",
                           umd, re.S | re.M).group()
        opened = re.search(r"VOID APIENTRY AdmissionUmdOpenResource\([^;]*?\)\s*\{.*?^}",
                           umd, re.S | re.M).group()
        icd = (agx / "windows/icd/agx_wgl_icd.cpp").read_text()
        icd_create = re.search(r"static BOOL wgl_kmt_create\(.*?^}", icd,
                               re.S | re.M).group()
        icd_open = re.search(r"static UINT wgl_redirect_open\(.*?^}", icd,
                             re.S | re.M).group()
        shared = (agx / "d3d12/agx_d3d12_shared_open.h").read_text()
        for body in (create, opened, icd_create, icd_open, shared):
            self.assertIn("AdmissionPresentationSizeCheck(", body)
            self.assertNotIn("0x1000000", body)
            self.assertNotIn("8192", body)


if __name__ == "__main__":
    unittest.main()
