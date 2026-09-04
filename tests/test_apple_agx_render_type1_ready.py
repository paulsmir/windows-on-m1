from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"


class AppleAgxRenderType1ReadyTests(unittest.TestCase):
    def test_start_publishes_readiness_only_after_all_runtime_owners(self):
        lifecycle = (RENDER / "src" / "lifecycle.c").read_text()
        ready = lifecycle.index(
            "APPLE_AGX_WDDM_REQUIRED_READY_MASK", lifecycle.index(
                "AdmissionDdiStartDevice("))

        self.assertLess(lifecycle.index("AdmissionScanoutStart(context)"), ready)
        self.assertLess(lifecycle.index("AdmissionObjectsStartAdapter("), ready)
        self.assertLess(ready, lifecycle.index(
            "AdmissionReceiptStartSucceeded", ready))
        self.assertIn(
            "InterlockedExchange(&context->FeatureReadyMask", lifecycle)

    def test_type1_writer_is_atomic_and_exact(self):
        lifecycle = (RENDER / "src" / "lifecycle.c").read_text()
        caps = lifecycle[lifecycle.index("case DXGKQAITYPE_DRIVERCAPS"):
                         lifecycle.index("case DXGKQAITYPE_WDDMDEVICECAPS")]

        self.assertIn("AppleAgxWddmFeatureContractReady", caps)
        self.assertIn("APPLE_AGX_WDDM_MANDATORY_CAPS_MASK", caps)
        for assignment in (
            "caps->GpuEngineTopology.NbAsymetricProcessingNodes = 1u",
            "caps->SchedulingCaps.MultiEngineAware = 1u",
            "caps->SchedulingCaps.PreemptionAware = 1u",
            "D3DKMDT_GRAPHICS_PREEMPTION_DMA_BUFFER_BOUNDARY",
            "D3DKMDT_COMPUTE_PREEMPTION_NONE",
            "caps->FlipCaps.FlipOnVSyncMmIo = 1u",
            "caps->FlipCaps.FlipIndependent = 1u",
            "caps->SupportNonVGA = TRUE",
            "caps->SupportPerEngineTDR = TRUE",
            "caps->SupportDirectFlip = TRUE",
            "caps->PresentationCaps.SupportKernelModeCommandBuffer = 1u",
        ):
            self.assertIn(assignment, caps)
        for unsupported in (
            "FlipImmediateMmIo = 1",
            "FlipInterval = 1",
            "SupportSmoothRotation = TRUE",
            "SupportMultiPlaneOverlay = TRUE",
            "MapAperture2Supported = 1",
        ):
            self.assertNotIn(unsupported, caps)

    def test_incomplete_contract_still_publishes_no_partial_caps(self):
        lifecycle = (RENDER / "src" / "lifecycle.c").read_text()
        caps = lifecycle[lifecycle.index("case DXGKQAITYPE_DRIVERCAPS"):
                         lifecycle.index("case DXGKQAITYPE_WDDMDEVICECAPS")]

        self.assertIn("AppleAgxWddmFeatureContractIncomplete", caps)
        self.assertIn("featureOutput.PublishCapsMask == 0u", caps)
        self.assertIn("status = STATUS_SUCCESS", caps)


if __name__ == "__main__":
    unittest.main()
