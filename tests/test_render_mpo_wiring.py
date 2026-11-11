"""S2 step 4: kernel wiring of the two-plane MPO contract.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
- The rule table answering "supported" for two planes whose broker records
  the scanout broker refuses (refusal codes 13..23 of the broker hand-back):
  that is a flip refused on hardware after Check3 said yes.  Checked by a
  generated sweep against a reference predicate written from the refusal
  list (tests/mpo_broker_reference_test.c).
- MPO advertised, or the two-plane Set3 path entered, without the
  Wom1MpoTwoPlanes-effective predicate (AGENTS capability rule; the bench
  image must be byte-for-byte today's behaviour with the switch off).
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


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class RenderMpoWiringTests(unittest.TestCase):
    def test_table_never_more_permissive_than_broker(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "mpo_broker_reference_test"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-Wconversion", "-Wsign-conversion", "-pedantic",
                "-fsanitize=address,undefined",
                "-I", str(DRIVER / "include"), "-I", str(SHARED / "include"),
                str(DRIVER / "tests/mpo_broker_reference_test.c"),
                str(DRIVER / "src/mpo_contract.c"),
                str(DRIVER / "src/mpo_layers.c"),
                "-o", str(binary),
            ], cwd=ROOT, check=True)
            run = subprocess.run([str(binary)], cwd=ROOT, capture_output=True,
                                 text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertIn("mpo_broker_reference_test: OK", run.stdout)

    def test_pure_layers_file_has_no_windows_dependency(self):
        for path in (DRIVER / "src/mpo_layers.c",
                     DRIVER / "include/mpo_layers.h"):
            text = path.read_text()
            includes = set(re.findall(r'^#include\s+[<"]([^>"]+)', text, re.M))
            self.assertLessEqual(
                includes,
                {"mpo_layers.h", "mpo_contract.h", "apple_agx_scanout.h"},
                path)
            self.assertNotRegex(text, r"\bunion\b", path)

    def test_mpo_caps_exist_only_behind_the_effective_predicate(self):
        lifecycle = (DRIVER / "src/lifecycle.c").read_text()
        caps = lifecycle[lifecycle.index("case DXGKQAITYPE_DRIVERCAPS"):
                         lifecycle.index("case DXGKQAITYPE_WDDMDEVICECAPS")]
        gate = caps.index("if (AdmissionScanoutMpoEffective(context)) {")
        block = caps[gate:caps.index("}", gate)]
        for field in ("SupportMultiPlaneOverlay = TRUE",
                      "MaxOverlayPlanes = 2u",
                      "SupportMultiPlaneOverlayImmediateFlip = FALSE",
                      "CursorScaledWithMultiPlaneOverlayPlane0 = FALSE",
                      "MaxQueuedMultiPlaneOverlayFlipVSync = 1u"):
            self.assertEqual(caps.count(field), 1, field)
            self.assertIn(field, block)
        self.assertIn("caps->SupportDirectFlip = TRUE", caps[:gate])

    def test_set3_two_plane_path_is_effective_only(self):
        display = (DRIVER / "src/display.c").read_text()
        body = function(
            display,
            "AdmissionDdiSetVidPnSourceAddressWithMultiPlaneOverlay3(\n")
        gate = body.index("AdmissionScanoutMpoEffective(context)")
        self.assertLess(gate, body.index("AdmissionMpoSetVidPnSourceAddress3"))
        # Today's one-plane checks follow the gate unchanged.
        self.assertLess(gate, body.index("Args->PlaneCount > 1u"))
        mpo = (DRIVER / "src/mpo_windows.c").read_text()
        self.assertIn("AdmissionScanoutMpoCommit", mpo)
        # One plane while slot 1 is off keeps today's V2 PRESENT path.
        self.assertIn("AdmissionDdiSetVidPnSourceAddress(Context, &legacy)", mpo)

    def test_ddis_registered_and_compiled(self):
        driver = (DRIVER / "src/driver.c").read_text()
        self.assertIn("initialization.DxgkDdiCheckMultiPlaneOverlaySupport3 =\n"
                      "      AdmissionDdiCheckMultiPlaneOverlaySupport3;", driver)
        self.assertIn("initialization.DxgkDdiGetMultiPlaneOverlayCaps =\n"
                      "      AdmissionDdiGetMultiPlaneOverlayCaps;", driver)
        project = (DRIVER / "AppleAgxRenderAdmission.vcxproj").read_text()
        for name in ("mpo_contract.c", "mpo_layers.c", "mpo_windows.c"):
            self.assertIn(f'<ClCompile Include="src\\{name}" />', project)

    def test_switch_is_read_once_at_start_and_defaults_off(self):
        scanout = (DRIVER / "src/scanout_windows.c").read_text()
        self.assertEqual(scanout.count('L"Wom1MpoTwoPlanes"'), 1)
        start = function(scanout,
                         "_Use_decl_annotations_ NTSTATUS AdmissionScanoutStart(")
        self.assertLess(start.index("RequestTwoLayers"),
                        start.index("AppleAgxFixedPanelStart("))
        reader = function(scanout, "static BOOLEAN AdmissionScanoutSwitchEnabled(")
        self.assertIn("*(const ULONG *)value->Data == 1u", reader)


if __name__ == "__main__":
    unittest.main()
