from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"
M1N1 = ROOT / "m1n1_windows" / "src"


class AppleAgxRenderHvcTests(unittest.TestCase):
    def test_portable_hvc_translation_contract(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "render_hvc_test"
            subprocess.run([
                os.environ.get("CC", "clang"),
                "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined",
                "-I", str(RENDER / "include"),
                str(RENDER / "tests" / "render_hvc_test.c"),
                str(RENDER / "src" / "render_hvc.c"),
                "-o", str(binary),
            ], check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)

    def test_project_links_hvc_and_physical_owner(self):
        project = (RENDER / "AppleAgxRenderAdmission.vcxproj").read_text()
        for source in (r"src\render_hvc.c", r"src\physical_memory_windows.c"):
            self.assertIn(source, project)
        self.assertNotIn("m1n1", project.lower())

    def test_local_abi_is_exactly_bound_to_m1n1(self):
        local = (RENDER / "include" / "hv_guest_ipa_pa_abi.h").read_text()
        m1n1 = (M1N1 / "hv_guest_ipa_pa.h").read_text()
        for define in (
            "HV_GUEST_IPA_PA_HVC_IMMEDIATE 0x4d31u",
            "HV_GUEST_IPA_PA_VERSION 1u",
            "HV_GUEST_IPA_PA_TRANSLATE 1u",
            "HV_GUEST_IPA_PA_MAX_PAGES 64u",
            "HV_GUEST_IPA_PA_PAGE_SIZE 0x1000ull",
            "HV_GUEST_IPA_PA_STAGE2_LEAF_SIZE 0x4000ull",
        ):
            self.assertIn(define, local)
            self.assertIn(define, m1n1)
        for field in (
            "version;", "operation;", "count;", "status;",
            "ipa[HV_GUEST_IPA_PA_MAX_PAGES];",
            "pa[HV_GUEST_IPA_PA_MAX_PAGES];",
        ):
            self.assertIn(field, local)
            self.assertIn(field, m1n1)

    def test_physical_owner_enforces_complete_adl_hvc_and_cleanup(self):
        source = (RENDER / "src" / "physical_memory_windows.c").read_text()
        cleanup = source[
            source.index("static VOID AdmissionPhysicalReleaseRaw"):
            source.index("static NTSTATUS AdmissionPhysicalCreateRaw")
        ]
        self.assertIn("DxgkCbCreatePhysicalMemoryObject", source)
        self.assertIn("DXGK_PHYSICAL_MEMORY_TYPE_CONTIGUOUS_MEMORY", source)
        self.assertIn("adlArgs.Flags.RequireContiguous = 1u", source)
        self.assertIn("allocation->Adl->PageCount != pageCount", source)
        self.assertIn("DxgkCbMapPhysicalMemory", source)
        self.assertIn("AdmissionHvcTranslatePages", source)
        self.assertIn("physicalPages[index] != physicalPages[0]", source)
        self.assertIn("ADMISSION_HVC_PHYSICAL_LIMIT - physicalPages[0]", source)
        self.assertLess(
            cleanup.index("DxgkCbUnmapPhysicalMemory"),
            cleanup.index("DxgkCbFreeAdl"),
        )
        self.assertLess(
            cleanup.index("DxgkCbFreeAdl"),
            cleanup.index("DxgkCbDestroyPhysicalMemoryObject"),
        )
        self.assertNotIn("GpuVirtualAddress = physicalPages", source)


if __name__ == "__main__":
    unittest.main()
