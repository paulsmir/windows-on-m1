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

    def _primary_run(self, contract_source):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "mpo_primary_test"
            source = Path(directory) / "mpo_contract.c"
            source.write_text(contract_source)
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-Wconversion", "-Wsign-conversion", "-pedantic",
                "-fsanitize=address,undefined",
                "-I", str(DRIVER / "include"),
                str(DRIVER / "tests/mpo_primary_test.c"), str(source),
                "-o", str(binary),
            ], cwd=ROOT, check=True)
            return subprocess.run([str(binary)], cwd=ROOT,
                                  capture_output=True, text=True)

    def test_one_plane_check3_never_promises_what_set3_refuses(self):
        # A1.1 invariant: Check3 TRUE for one plane => the V2 path accepts it.
        run = self._primary_run((DRIVER / "src/mpo_contract.c").read_text())
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("mpo_primary_test: OK", run.stdout)

    def test_mutant_without_a_check3_rule_is_caught(self):
        text = (DRIVER / "src/mpo_contract.c").read_text()
        rule = "  if (Plane->AllocationBytes != (unsigned long long)Input->PanelStride *"
        self.assertEqual(text.count(rule), 1)
        mutant = text.replace(rule, "  if (0 && Plane->AllocationBytes != "
                                    "(unsigned long long)Input->PanelStride *")
        run = self._primary_run(mutant)
        self.assertNotEqual(run.returncode, 0)

    def test_one_plane_rule_is_wired_into_check3_and_set3(self):
        mpo = (DRIVER / "src/mpo_windows.c").read_text()
        check = function(mpo, "_Use_decl_annotations_ NTSTATUS "
                              "AdmissionDdiCheckMultiPlaneOverlaySupport3(")
        # STRETCH-1: AdmissionMpoEvaluateOne answers the full-screen
        # primary with AdmissionMpoEvaluatePrimary (mpo_contract_test.c
        # TestEvaluateOne) and anything else with the layer-record rules.
        self.assertIn("if (effective && Args->PlaneCount == 1u)\n"
                      "    (void)AdmissionMpoEvaluateOne(&input, &result);",
                      check)
        set3 = function(mpo, "_Use_decl_annotations_ NTSTATUS "
                             "AdmissionMpoSetVidPnSourceAddress3(")
        self.assertNotIn("AdmissionMpoEvaluatePrimary(&", set3)
        one = set3[set3.index("if (next[0].Enabled && listed[0]) {"):]
        self.assertLess(one.index("AdmissionMpoEvaluateOne(&input, &result)"),
                        one.index("if (result.OnePlaneRecord != 0u) {"))
        self.assertLess(one.index("if (result.OnePlaneRecord != 0u) {"),
                        one.index("AdmissionDdiSetVidPnSourceAddress(Context"))
        record = one[one.index("if (result.OnePlaneRecord != 0u) {"):
                     one.index("AdmissionDdiSetVidPnSourceAddress(Context")]
        self.assertIn("AdmissionMpoBuildRemoval(&next[0].Input, &records[0])",
                      record)
        self.assertIn("AdmissionScanoutMpoCommit(Context, records, 1u,", record)

    def test_stretch_caps_and_table_follow_the_broker_bit(self):
        """STRETCH-1: no stretch reported or accepted without broker bit 8."""
        mpo = (DRIVER / "src/mpo_windows.c").read_text()
        caps = function(mpo, "_Use_decl_annotations_ NTSTATUS "
                             "AdmissionDdiGetMultiPlaneOverlayCaps(")
        self.assertIn("AdmissionMpoCapsFor(effective ? 1u : 0u,\n"
                      "                      AdmissionScanoutMpoScaledSource(context) ? 1u : 0u,\n"
                      "                      (unsigned int)ADMISSION_MPO_SCALER_FILTER_CURRENT,", caps)
        for field in ("StretchRGB = derived.StretchRgb",
                      "BilinearFilter = derived.BilinearFilter",
                      "HighFilter = derived.HighFilter",
                      "&derived.MaxStretchBits", "&derived.MaxShrinkBits"):
            self.assertIn(field, caps)
        build = function(mpo, "static VOID AdmissionMpoInput(")
        self.assertIn("Input->ScalingSupported = AdmissionScanoutMpoScaledSource(Context) ? 1u : 0u;",
                      build)
        self.assertIn("Input->ScalerFilter = (unsigned int)ADMISSION_MPO_SCALER_FILTER_CURRENT;",
                      build)
        plane = function(mpo, "static VOID AdmissionMpoPlaneInput(")
        self.assertIn("Plane->StretchQuality = (unsigned int)Attributes->StretchQuality;", plane)
        scanout = (DRIVER / "src/scanout_windows.c").read_text()
        scaled = function(scanout, "_Use_decl_annotations_ BOOLEAN "
                                   "AdmissionScanoutMpoScaledSource(")
        self.assertIn("AppleAgxFixedPanelTwoLayers(&runtime->Panel)", scaled)
        self.assertIn("APPLE_AGX_SCANOUT_CAP_SCALED_SOURCE", scaled)
        # The filter stays unknown until measured on the panel.
        contract = (DRIVER / "include/mpo_contract.h").read_text()
        self.assertIn("#define ADMISSION_MPO_SCALER_FILTER_CURRENT "
                      "AdmissionMpoScalerFilterUnknown", contract)
        shared = (SHARED / "include/apple_agx_scanout.h").read_text()
        self.assertIn("#define APPLE_AGX_SCANOUT_CAP_SCALED_SOURCE (1u << 8u)", shared)

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
