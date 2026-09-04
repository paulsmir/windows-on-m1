from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"


class AppleAgxRenderPagingTests(unittest.TestCase):
    def test_buildpagingbuffer_uses_existing_plans_and_real_executor(self):
        source = (RENDER / "src" / "paging_windows.c").read_text()
        runtime = (RENDER / "src" / "memory_runtime_windows.c").read_text()
        callbacks = (RENDER / "src" / "callbacks.c").read_text()
        self.assertNotIn("FAIL2(AdmissionDdiBuildPagingBuffer", callbacks)
        for operation in (
            "DXGK_OPERATION_MAP_APERTURE_SEGMENT",
            "DXGK_OPERATION_UNMAP_APERTURE_SEGMENT",
            "DXGK_OPERATION_TRANSFER",
            "DXGK_OPERATION_FILL",
            "DXGK_OPERATION_DISCARD_CONTENT",
        ):
            self.assertIn(operation, source)
        self.assertIn("AdmissionMemoryPlanTransfer", source)
        self.assertIn("AdmissionMemoryPlanFill", source)
        self.assertIn("AdmissionMemoryPlanDiscard", source)
        self.assertIn("AdmissionMemoryRuntimeExecutePaging", runtime)
        self.assertIn("AppleAgxPhysicalPagingExecute", runtime)
        self.assertIn("MmGetSystemAddressForMdlSafe", runtime)

    def test_paging_record_is_pointer_bounded_and_not_falsely_ready(self):
        header = (RENDER / "include" / "render_paging.h").read_text()
        runtime = (RENDER / "src" / "memory_runtime_windows.c").read_text()
        self.assertIn("ADMISSION_PAGING_MAGIC", header)
        self.assertIn("APPLE_AGX_PHYSICAL_PAGING_PLAN Plan", header)
        self.assertIn("void *SystemMdl", header)
        self.assertNotIn("AdmissionMemoryMarkPagingReady", runtime)


if __name__ == "__main__":
    unittest.main()
