from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"


class AppleAgxRenderUatTests(unittest.TestCase):
    def test_runtime_uses_physical_owner_context63_and_16k_uat(self):
        source = (RENDER / "src" / "memory_runtime_windows.c").read_text()
        self.assertIn("AdmissionPhysicalOwnerInitialize", source)
        self.assertIn("AppleAgxMemoryAllocateAligned", source)
        self.assertIn("ADMISSION_ALLOCATION_ALIGNMENT", source)
        self.assertIn("AppleAgxResidencyContextCreate", source)
        self.assertIn("ADMISSION_MEMORY_UAT_CONTEXT", source)
        self.assertIn("AppleAgxResidencyMap64K", source)
        self.assertIn("ADMISSION_LOCAL_GPU_VA", source)
        self.assertIn("AppleAgxUatEncodeTtbrPair", source)
        self.assertIn("AppleAgxUatPublishJ313Context", source)
        self.assertIn("APPLE_AGX_UAT_PAGE_SIZE_16K", source)
        self.assertNotIn("GpuVirtualAddress = runtime->LocalObject.DeviceAddress", source)

    def test_start_and_stop_preserve_reverse_ownership_order(self):
        source = (RENDER / "src" / "memory_runtime_windows.c").read_text()
        destroy = source[
            source.index("static NTSTATUS AdmissionMemoryRuntimeDestroy"):
            source.index("_Use_decl_annotations_ NTSTATUS AdmissionMemoryRuntimeStart")
        ]
        order = [
            "AppleAgxUatUnpublishJ313",
            "AppleAgxResidencyUnmap64K",
            "AppleAgxResidencyContextDestroy",
            "AppleAgxMemoryRelease",
            "AdmissionPhysicalOwnerDestroy",
        ]
        positions = [destroy.index(item) for item in order]
        self.assertEqual(positions, sorted(positions))

    def test_memory_is_not_ready_before_buildpagingbuffer(self):
        source = (RENDER / "src" / "memory_runtime_windows.c").read_text()
        self.assertIn("AdmissionMemoryMarkUatReady", source)
        self.assertNotIn("AdmissionMemoryMarkPagingReady", source)
        lifecycle = (RENDER / "src" / "lifecycle.c").read_text()
        self.assertIn("AdmissionMemoryRuntimeStart(context)", lifecycle)
        self.assertIn("AdmissionMemoryRuntimeStop(context)", lifecycle)


if __name__ == "__main__":
    unittest.main()
