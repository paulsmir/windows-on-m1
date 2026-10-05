from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers" / "apple-agx" / "shared"


class AppleAgxGdiContractTests(unittest.TestCase):
    def test_portable_gdi_contract(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "apple_agx_gdi_test"
            command = [
                os.environ.get("CC", "clang"),
                "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined",
                "-I", str(SHARED / "include"),
                str(SHARED / "tests" / "apple_agx_gdi_test.c"),
                str(SHARED / "src" / "apple_agx_gdi.c"),
                str(SHARED / "src" / "apple_agx_memory.c"),
                "-o", str(binary),
            ]
            subprocess.run(command, check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)

    def test_windows_renderkm_fails_before_dma_shadow_mutation(self):
        windows = ROOT / "drivers" / "apple-agx" / "windows" / "src"
        gdi = (windows / "gdi_windows.c").read_text()
        adapter = (windows / "adapter.c").read_text()
        render = gdi[
            gdi.index("_Use_decl_annotations_ NTSTATUS AppleAgxDdiRenderKm") :
            gdi.index("_Use_decl_annotations_ NTSTATUS AppleAgxDdiPatch")
        ]
        semantic_gate = render.index(
            "APPLE_AGX_EXP208_SUPPORTED_GDI_PRIMITIVE_MASK == 0u"
        )
        self.assertLess(semantic_gate, render.index("AppleAgxDmaShadowOpen"))
        self.assertIn("return STATUS_NOT_SUPPORTED;", render[semantic_gate:])
        self.assertIn("SupportKernelModeCommandBuffer = 0", adapter)


if __name__ == "__main__":
    unittest.main()
