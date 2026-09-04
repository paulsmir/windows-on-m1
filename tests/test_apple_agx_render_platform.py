from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"


class AppleAgxRenderPlatformTests(unittest.TestCase):
    def test_production_lifetime_uses_existing_external_image_provider(self):
        source = (RENDER / "src" / "backend_platform_windows.c").read_text()
        lifecycle = (RENDER / "src" / "lifecycle.c").read_text()
        project = (RENDER / "AppleAgxRenderAdmission.vcxproj").read_text()

        self.assertIn("AppleAgxPlatformProviderInitialize", source)
        self.assertIn("ProviderConfig.ExternalRender.BuildJob", source)
        self.assertIn("AdmissionMemoryRuntimeBorrowIo", source)
        self.assertIn("AdmissionMemoryRuntimeContextPublished", source)
        self.assertNotIn("CompleteUnavailable", source)
        self.assertNotIn("RetireUnavailable", source)
        self.assertIn("APPLE_AGX_PLATFORM_EXTERNAL_RENDER_ONLY=1", project)
        self.assertIn(r"src\backend_platform_windows.c", project)
        self.assertLess(lifecycle.index("AdmissionMemoryRuntimeStart(context)"),
                        lifecycle.index("AdmissionBackendImageStart(context)"))
        self.assertLess(lifecycle.index("AdmissionBackendImageStart(context)"),
                        lifecycle.index("AdmissionPlatformRuntimeStart(context)"))

    def test_completion_is_dual_event_driven_and_exact_fence_only(self):
        source = (RENDER / "src" / "backend_platform_windows.c").read_text()
        submit = (RENDER / "src" / "submission_windows.c").read_text()

        self.assertIn("AppleAgxPlatformProviderPoll", source)
        self.assertIn("AppleAgxSchedulerActiveFence", source)
        self.assertIn("AppleAgxSchedulerCompleteActiveFence", source)
        self.assertIn("AdmissionRenderPacketComplete", source)
        self.assertIn("AdmissionBackendImageReleaseSubmission", source)
        self.assertIn("AppleAgxCompletionTransactionCanReport", source)
        self.assertIn("DXGK_INTERRUPT_DMA_COMPLETED", source)
        self.assertIn("AdmissionPlatformRuntimeSubmit", submit)
        self.assertNotIn("AdmissionSchedulerRecordCompletion(", source)

    def test_bootstrap_profile_excludes_physical_agx_irq_routes(self):
        source = (RENDER / "src" / "backend_platform_windows.c").read_text()
        self.assertIn("memory_count == 4u", source)
        self.assertIn("interrupt_count == 1u", source)
        self.assertIn(
            "J313_AGX_ABI_ADMISSION_SYNTHETIC_SCANOUT_GUEST_INTID", source)
        for irq in range(880, 889):
            self.assertNotIn(str(irq), source)


if __name__ == "__main__":
    unittest.main()
