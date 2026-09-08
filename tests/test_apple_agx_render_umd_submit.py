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
        self.assertIn("Args->pPatchLocationListOut = location + 1", wrapper)
        self.assertNotIn("--Args->PatchLocationListOutSize", wrapper)
        self.assertIn("AdmissionGdiReceiptBeginWindows", wrapper)
        self.assertNotIn("pPatchLocationListIn != NULL", wrapper)
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
        self.assertNotIn("createDevice.Flags.LegacyMode = 1u", source)
        self.assertIn("createContext.Flags.Value = 0u", source)
        self.assertIn("createContext.ClientHint = D3DKMT_CLIENTHINT_UNKNOWN", source)
        self.assertNotIn("createContext.ClientHint = D3DKMT_CLIENTHINT_OPENGL", source)
        self.assertIn("D3DKMTCreatePagingQueue", source)
        self.assertIn("D3DKMTMakeResident", source)
        self.assertIn("PagingFenceValue", source)
        self.assertIn("FenceValueCPUVirtualAddress", source)
        self.assertIn("D3DKMTDestroyPagingQueue", source)
        self.assertIn("D3DKMT_RENDER render = {0}", source)
        self.assertNotIn("render.Flags.RenderKm = 1", source)
        self.assertEqual(source.count("D3DKMTRender(&render)"), 1)
        self.assertNotIn("D3DKMTLock2(", source)
        self.assertIn("D3DDDIFMT_A8R8G8B8, 0u, &allocation", source)
        self.assertIn("createContext.pCommandBuffer", source)
        self.assertIn("createContext.pAllocationList", source)
        self.assertIn("createContext.pPatchLocationList", source)
        self.assertIn("destroy.Flags.SynchronousDestroy = 1", source)
        self.assertIn("BUFFERS device_command=", source)
        self.assertIn("RENDER_OUT pass=", source)
        self.assertIn("render_umd_command.h", project)
        self.assertIn("apple_agx_exp208_gdi.h", source)
        self.assertGreaterEqual(
            source.count("APPLE_AGX_EXP208_FRAMEBUFFER_WIDTH"), 2
        )
        self.assertGreaterEqual(
            source.count("APPLE_AGX_EXP208_FRAMEBUFFER_HEIGHT"), 2
        )
        self.assertIn("APPLE_AGX_EXP208_FRAMEBUFFER_BASE_COLOR", source)
        self.assertIn("APPLE_AGX_EXP208_FRAMEBUFFER_BAND_COLOR", source)
        self.assertIn("Sleep(15000u);", source)
        self.assertIn(r"..\shared\include", project)
        self.assertIn("<RuntimeLibrary>MultiThreaded</RuntimeLibrary>", project)

    def test_visible_agx_destination_is_explicit_windows_owned_allocation(self):
        producer = (
            WINDOWS / "one-shot" / "apple_agx_d3dkmt_render.c"
        ).read_text()
        patch = (RENDER / "src" / "gdi_windows.c").read_text()
        memory = (RENDER / "src" / "memory_runtime_windows.c").read_text()

        self.assertIn("D3DDDI_ALLOCATIONINFO allocationInfo[2]", producer)
        self.assertIn("ADMISSION_ALLOCATION_DESCRIPTION allocation[2]", producer)
        self.assertIn("APPLE_AGX_SCANOUT_J313_WIDTH", producer)
        self.assertIn("APPLE_AGX_SCANOUT_J313_HEIGHT", producer)
        self.assertEqual(
            producer.count("D3DKMTCreateAllocation(&createAllocation)"),
            2,
        )
        self.assertEqual(
            producer.count("createAllocation.NumAllocations = 1u"),
            2,
        )
        self.assertIn(
            "createAllocation.pAllocationInfo = &allocationInfo[0]",
            producer,
        )
        self.assertIn(
            "createAllocation.pAllocationInfo = &allocationInfo[1]",
            producer,
        )
        self.assertNotIn(
            "createAllocation.NumAllocations = ARRAYSIZE(allocationInfo)",
            producer,
        )
        self.assertIn("makeResident.NumAllocations = ARRAYSIZE(allocationHandles)", producer)
        self.assertIn("render.AllocationCount = ARRAYSIZE(allocationHandles)", producer)
        self.assertIn("createContext.pAllocationList[1].WriteOperation = 1u", producer)
        self.assertNotIn(
            "allocation[1].Reserved = ADMISSION_UMD_CORRELATION_COOKIE",
            producer,
        )

        self.assertIn("AdmissionVisibleAgxResolveDestination", patch)
        self.assertIn("const UINT index = 1u", patch)
        self.assertIn("entry->SegmentId != ADMISSION_MEMORY_LOCAL_SEGMENT", patch)
        self.assertIn("description->Size != APPLE_AGX_SCANOUT_J313_SURFACE_SIZE", patch)
        self.assertNotIn("Adapter->VisibleAgxDestination = view", patch)
        self.assertIn("*VisibleDestination = view", patch)
        self.assertIn(
            "description.VisibleDestinationAllocationToken = VisibleAllocationToken",
            patch,
        )
        self.assertIn(
            "description.VisibleDestinationGpuVa =",
            patch,
        )

        self.assertNotIn("ADMISSION_VISIBLE_AGX_DESTINATION_OFFSET", memory)
        self.assertNotIn(
            "Context->Memory.LocalAllocationBytes =",
            memory[memory.index("NTSTATUS AdmissionMemoryRuntimeStart("):],
        )
        self.assertIn(
            "Context->Memory.LocalAllocationBytes !=\n"
            "          ADMISSION_LOCAL_ALLOCATION_BYTES",
            memory,
        )

    def test_visible_qualification_keeps_destination_alive_past_latch(self):
        producer = (
            WINDOWS / "one-shot" / "apple_agx_d3dkmt_render.c"
        ).read_text()
        backend = (RENDER / "src" / "backend_platform_windows.c").read_text()
        complete = backend[
            backend.index("static APPLE_AGX_BACKEND_BOOL AdmissionBackendComplete("):
            backend.index("static VOID AdmissionPlatformWorkerFinished(")
        ]
        self.assertEqual(backend.count("AdmissionScanoutPresentAgxResult("), 1)
        self.assertGreater(
            complete.index("AdmissionScanoutPresentAgxResult("),
            complete.index("DxgkCbSynchronizeExecution("),
        )
        after_present = complete[
            complete.index("AdmissionScanoutPresentAgxResult("):
            complete.index("static APPLE_AGX_BACKEND_BOOL AdmissionBackendRetire(")
        ]
        self.assertNotIn("return APPLE_AGX_BACKEND_FALSE", after_present)
        self.assertIn("Sleep(10000u);", producer)
        self.assertLess(
            producer.index("Sleep(10000u);"),
            producer.index("cleanup:"),
        )

    def test_fullscreen_agx_output_is_latched_without_cpu_scale(self):
        producer = (
            WINDOWS / "one-shot" / "apple_agx_d3dkmt_render.c"
        ).read_text()
        backend = (RENDER / "src" / "backend_platform_windows.c").read_text()
        scanout = (RENDER / "src" / "scanout_windows.c").read_text()

        self.assertGreaterEqual(
            producer.count("APPLE_AGX_EXP208_FRAMEBUFFER_WIDTH"), 2
        )
        self.assertGreaterEqual(
            producer.count("APPLE_AGX_EXP208_FRAMEBUFFER_HEIGHT"), 2
        )
        self.assertIn("ULONG targetBytes = framebuffer", backend)
        self.assertIn("(PUCHAR)output->Data - outputOffset", backend)
        self.assertIn("expandedObjects[] = {64u, 65u, 67u}", backend)
        self.assertIn("BOOLEAN directFramebuffer = FALSE", scanout)
        self.assertIn(
            "Packet->DestinationPhysical == SourcePhysicalAddress",
            scanout,
        )
        self.assertIn("AdmissionVisibleAgxUseFramebuffer", scanout)
        direct = scanout[scanout.index("directFramebuffer ="):]
        self.assertLess(
            direct.index("AdmissionVisibleAgxUseFramebuffer"),
            direct.index("AppleAgxFixedPanelQueuePresent"),
        )


if __name__ == "__main__":
    unittest.main()
