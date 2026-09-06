from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"
WINDOWS = ROOT / "drivers" / "apple-agx" / "windows"


class AppleAgxRenderUmdSubmitTests(unittest.TestCase):
    def test_pointer_free_command_contract(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "render_umd_command_test"
            subprocess.run([
                os.environ.get("CC", "clang"),
                "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined",
                "-I", str(RENDER / "include"),
                str(RENDER / "tests" / "render_umd_command_test.c"),
                str(RENDER / "src" / "render_umd_command.c"),
                "-o", str(binary),
            ], check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)

    def test_kmd_implements_user_render_as_one_atomic_wddm_pipeline(self):
        callbacks = (RENDER / "src" / "callbacks.c").read_text()
        wrapper = (RENDER / "src" / "umd_render_windows.c").read_text()
        patch = (RENDER / "src" / "gdi_windows.c").read_text()
        submit = (RENDER / "src" / "submission_windows.c").read_text()
        project = (RENDER / "AppleAgxRenderAdmission.vcxproj").read_text()

        self.assertNotIn("FAIL2(AdmissionDdiRender,", callbacks)
        self.assertIn("__try", wrapper)
        self.assertIn("__except(EXCEPTION_EXECUTE_HANDLER)", wrapper)
        self.assertIn("AdmissionUmdColorFillCommandValid", wrapper)
        self.assertIn("AdmissionGdiPrepareColorFill", wrapper)
        self.assertIn("AppleAgxDmaShadowAppend", wrapper)
        self.assertIn("AdmissionGdiReceiptBeginWindows", wrapper)
        self.assertIn("pPatchLocationListIn != NULL", wrapper)
        self.assertIn("PatchLocationListInSize != 0u", wrapper)
        patch_render = patch[patch.index("NTSTATUS AdmissionDdiPatch("):]
        self.assertIn("ADMISSION_CONTEXT_SYSTEM", patch_render)
        self.assertIn("ADMISSION_CONTEXT_SYSTEM", submit)
        self.assertNotIn(
            "(context->Object.Flags & ADMISSION_CONTEXT_GDI) == 0u",
            patch_render,
        )
        self.assertNotIn(
            "(render_context->Object.Flags & ADMISSION_CONTEXT_GDI) == 0u",
            submit,
        )
        self.assertIn(r"src\render_umd_command.c", project)
        self.assertIn(r"src\umd_render_windows.c", project)

    def test_non_gdi_context_has_private_dma_shadow(self):
        callbacks = (RENDER / "src" / "callbacks.c").read_text()
        context_info = callbacks[
            callbacks.index("RtlZeroMemory(&Args->ContextInfo"):
            callbacks.index("Args->hContext = context;")
        ]
        self.assertLess(
            context_info.index("DmaBufferPrivateDataSize"),
            context_info.index("if (Args->Flags.GdiContext)"),
        )

    def test_producer_uses_current_non_test_d3dkmt_contract(self):
        source = (WINDOWS / "one-shot" / "apple_agx_d3dkmt_render.c").read_text()
        project = (WINDOWS / "one-shot" / "AppleAgxD3dKmRender.vcxproj").read_text()
        self.assertNotIn("apple_agx_one_shot_abi.h", source)
        self.assertNotIn("TestContext", source)
        self.assertNotIn("createContext.pPrivateDriverData", source)
        self.assertNotIn("createContext.PrivateDriverDataSize", source)
        self.assertIn("GetProcAddress", source)
        self.assertIn('"D3DKMTEnumAdapters3"', source)
        self.assertIn("PFND3DKMT_ENUMADAPTERS3", source)
        self.assertIn("KMTQAITYPE_ADAPTERTYPE", source)
        self.assertIn("adapterType.RenderSupported", source)
        self.assertIn("adapterType.DisplaySupported", source)
        self.assertIn("adapterType.PostDevice", source)
        self.assertIn("!adapterType.SoftwareDevice", source)
        self.assertNotIn("adapters[index].NumOfSources == 1u", source)
        self.assertIn("matchingAdapters != 1u", source)
        self.assertIn("ADAPTER index=", source)
        self.assertIn("adapterType.Value", source)
        self.assertNotIn("D3DKMTOpenAdapterFromGdiDisplayName", source)
        self.assertNotIn("D3DKMTOpenAdapterFromLuid", source)
        self.assertIn("createContext.Flags.Value = 0u", source)
        self.assertIn("D3DKMT_RENDER render = {0}", source)
        self.assertNotIn("render.Flags.RenderKm = 1", source)
        self.assertEqual(source.count("D3DKMTRender(&render)"), 1)
        self.assertIn("createContext.pCommandBuffer", source)
        self.assertIn("createContext.pAllocationList", source)
        self.assertIn("createContext.pPatchLocationList", source)
        self.assertIn("destroy.Flags.SynchronousDestroy = 1", source)
        self.assertIn("BUFFERS device_command=", source)
        self.assertIn("RENDER_OUT command=", source)
        self.assertIn("render_umd_command.h", project)
        self.assertIn("<RuntimeLibrary>MultiThreaded</RuntimeLibrary>", project)


if __name__ == "__main__":
    unittest.main()
