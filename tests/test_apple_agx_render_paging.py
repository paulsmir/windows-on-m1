from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"


class AppleAgxRenderPagingTests(unittest.TestCase):
    def test_portable_paging_submission_contract(self):
        import os
        import subprocess
        import tempfile
        shared = ROOT / "drivers" / "apple-agx" / "shared"
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "render_paging_test"
            subprocess.run([
                os.environ.get("CC", "clang"),
                "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined",
                "-I", str(RENDER / "include"),
                "-I", str(shared / "include"),
                str(RENDER / "tests" / "render_paging_test.c"),
                str(RENDER / "src" / "render_paging.c"),
                "-o", str(binary),
            ], check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)

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

    def test_paging_only_submit_has_worker_interrupt_and_dpc_completion(self):
        source = (RENDER / "src" / "paging_windows.c").read_text()
        interrupt = (RENDER / "src" / "interrupt.c").read_text()
        callbacks = (RENDER / "src" / "callbacks.c").read_text()
        self.assertNotIn("FAIL2(AdmissionDdiSubmitCommand,", callbacks)
        self.assertIn("if (context == NULL || Args == NULL", source)
        self.assertIn("!Args->Flags.Paging", source)
        self.assertIn("ADMISSION_MAX_PAGING_RECORDS", source)
        self.assertIn("AdmissionCpuQueueSubmit", source)
        self.assertIn("AdmissionPagingRecordsValid", source)
        self.assertIn("IoQueueWorkItem", source)
        self.assertIn("AdmissionMemoryRuntimeExecutePaging", source)
        self.assertIn("DxgkCbSynchronizeExecution", source)
        self.assertIn("DXGK_INTERRUPT_DMA_COMPLETED", source)
        self.assertIn("DXGK_INTERRUPT_DMA_FAULTED", source)
        self.assertIn("DxgkCbQueueDpc", source)
        self.assertNotIn("DxgkCbNotifyDpc(", source)
        self.assertEqual(interrupt.count("DxgkCbNotifyDpc("), 1)
        self.assertIn("AdmissionPagingDpc(context)", interrupt)
        self.assertIn("AdmissionMemoryMarkPagingReady", source)


if __name__ == "__main__":
    unittest.main()
