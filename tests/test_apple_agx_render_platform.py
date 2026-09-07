from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"


class AppleAgxRenderPlatformTests(unittest.TestCase):
    def test_external_render_materializes_firmware_queue_image_before_mapping(self):
        platform = (RENDER / "src" / "backend_platform_windows.c").read_text()
        prepare = platform.index("AppleAgxInitdataMemoryPrepareBroker(")
        seed = platform.index("Context->BackendImage.Objects", prepare)
        bind = platform.index("AppleAgxRenderSharedMemoryBindRelocationObjects(", prepare)
        relocate = platform.index("AppleAgxApplyRelocations(", bind)
        firmware = platform.index("AppleAgxFirmwareProviderInitialize(", relocate)
        self.assertLess(prepare, bind)
        self.assertLess(seed, bind)
        self.assertLess(bind, relocate)
        self.assertLess(relocate, firmware)
        self.assertIn("QueueImageReady", platform)

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
        dispatch = (RENDER / "src" / "work_queue_windows.c").read_text()

        self.assertIn("AppleAgxPlatformProviderPoll", source)
        memory = (RENDER / "src" / "memory_runtime_windows.c").read_text()
        self.assertIn("APPLE_AGX_RENDER_TEMPLATE_FIXED_INPUT_FIRST_OBJECT", memory)
        self.assertIn("0x1100010000ULL", memory)
        self.assertIn("AppleAgxUatGpuSharedReadWrite", memory)
        self.assertIn("AppleAgxSchedulerActiveFence", source)
        self.assertIn("AppleAgxSchedulerCompleteActiveFence", source)
        self.assertIn("AdmissionRenderPacketComplete", source)
        self.assertIn("AdmissionBackendImageReleaseSubmission", source)
        self.assertIn("AppleAgxCompletionTransactionCanReport", source)
        self.assertIn("DXGK_INTERRUPT_DMA_COMPLETED", source)
        self.assertIn("AdmissionDispatchQueuedWork", submit)
        self.assertIn("AdmissionPlatformRuntimeSubmit", dispatch)
        self.assertNotIn("AdmissionSchedulerRecordCompletion(", source)

    def test_gdi_hardware_receipt_observes_existing_pipeline_only(self):
        platform = (RENDER / "src" / "backend_platform_windows.c").read_text()
        scheduler = (RENDER / "src" / "scheduler_windows.c").read_text()
        lifecycle = (RENDER / "src" / "lifecycle.c").read_text()
        project = (RENDER / "AppleAgxRenderAdmission.vcxproj").read_text()
        self.assertIn("AdmissionGdiReceiptBackendWindows", platform)
        self.assertIn("AdmissionGdiReceiptCompleteWindows", platform)
        self.assertLess(
            platform.index("DxgkCbSynchronizeExecution", platform.index("static APPLE_AGX_BACKEND_BOOL AdmissionBackendComplete")),
            platform.index("AdmissionGdiReceiptCompleteWindows", platform.index("static APPLE_AGX_BACKEND_BOOL AdmissionBackendComplete"))
        )
        self.assertIn("AdmissionGdiReceiptProgressWindows", platform)
        self.assertIn("AdmissionGdiReceiptDpcWindows", scheduler)
        self.assertIn("AdmissionFlushGdiReceipt", lifecycle)
        self.assertIn(r"src\render_gdi_receipt.c", project)
        self.assertIn(r"src\gdi_receipt_windows.c", project)

    def test_bootstrap_profile_excludes_physical_agx_irq_routes(self):
        source = (RENDER / "src" / "backend_platform_windows.c").read_text()
        self.assertIn("memory_count == 4u", source)
        self.assertIn("interrupt_count == 1u", source)
        self.assertIn(
            "J313_AGX_ABI_ADMISSION_SYNTHETIC_SCANOUT_GUEST_INTID", source)
        for irq in range(880, 889):
            self.assertNotIn(str(irq), source)

    def test_per_engine_tdr_quiesces_retires_and_restarts_same_backend(self):
        platform = (RENDER / "src" / "backend_platform_windows.c").read_text()
        scheduler = (RENDER / "src" / "scheduler_windows.c").read_text()

        self.assertIn("AdmissionPlatformRuntimeReset", scheduler)
        self.assertIn("AdmissionPlatformRuntimeResponsive", scheduler)
        self.assertIn("AppleAgxBackendRuntimeStop", platform)
        self.assertIn("AppleAgxPlatformProviderDestroy", platform)
        self.assertIn("AppleAgxPlatformProviderInitialize", platform)
        self.assertIn("AppleAgxBackendRuntimeStart", platform)
        self.assertIn("AdmissionBackendRetire", platform)
        self.assertIn("runtime->Backend.QueuesQuiesced", platform)
        active = scheduler[scheduler.index(
            "if (packetState == AdmissionRenderPacketActive)"):]
        self.assertNotIn("return STATUS_DEVICE_BUSY", active.split(
            "if (packetState == AdmissionRenderPacketQueued)")[0])


if __name__ == "__main__":
    unittest.main()
