from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"


class AppleAgxRenderPlatformStageTests(unittest.TestCase):
    def test_platform_start_persists_each_existing_boundary(self):
        header = (RENDER / "include" / "render_admission.h").read_text()
        receipts = (RENDER / "src" / "receipts.c").read_text()
        platform = (RENDER / "src" / "backend_platform_windows.c").read_text()
        start = platform[platform.index("AdmissionPlatformRuntimeStart("):
                         platform.index("AdmissionPlatformRuntimeStop(")]

        for stage in (
            "AdmissionPlatformEntered",
            "AdmissionPlatformResources",
            "AdmissionPlatformRuntimeAllocated",
            "AdmissionPlatformMemoryIo",
            "AdmissionPlatformSnapshot",
            "AdmissionPlatformSgxMap",
            "AdmissionPlatformHandoffMap",
            "AdmissionPlatformHandoffBind",
            "AdmissionPlatformInitdata",
            "AdmissionPlatformFirmwareProvider",
            "AdmissionPlatformQueueProvider",
            "AdmissionPlatformBackendStart",
            "AdmissionPlatformWorkItem",
            "AdmissionPlatformComplete",
        ):
            self.assertIn(stage, header)
            self.assertRegex(
                start,
                rf"AdmissionRecordPlatformStage\(Context,\s*{re.escape(stage)}",
            )
        self.assertIn('L"Wom1PlatformStage"', receipts)
        self.assertIn('L"Wom1PlatformStatus"', receipts)
        stop = platform[platform.index("AdmissionPlatformRuntimeStop("):
                        platform.index("AdmissionPlatformRuntimeReset(")]
        self.assertNotIn("AdmissionRecordPlatformStage", stop)

    def test_translated_interrupt_is_a_runtime_vector_not_firmware_gsi(self):
        platform = (RENDER / "src" / "backend_platform_windows.c").read_text()
        validate = platform[
            platform.index("AdmissionPlatformValidateResources("):
            platform.index("AdmissionPlatformReadSnapshot(")
        ]

        self.assertIn("descriptor->u.Interrupt.Vector == 0u", validate)
        self.assertIn(
            "descriptor->ShareDisposition != CmResourceShareDeviceExclusive",
            validate,
        )
        self.assertIn(
            "descriptor->Flags != CM_RESOURCE_INTERRUPT_LATCHED",
            validate,
        )
        self.assertNotIn(
            "J313_AGX_ABI_ADMISSION_SYNTHETIC_SCANOUT_GUEST_INTID", validate
        )

    def test_config_snapshot_uses_supported_aligned_mmio_reads(self):
        platform = (RENDER / "src" / "backend_platform_windows.c").read_text()
        snapshot = platform[
            platform.index("AdmissionPlatformReadSnapshot("):
            platform.index("AdmissionAscRange(")
        ]

        self.assertIn("ULONG wire[", snapshot)
        self.assertIn("RtlZeroMemory(wire, sizeof(wire))", snapshot)
        self.assertIn(
            "index = APPLE_AGX_CONFIG_MMIO_OFFSET / sizeof(ULONG)", snapshot
        )
        self.assertIn("READ_REGISTER_ULONG", snapshot)
        self.assertNotIn("READ_REGISTER_UCHAR", snapshot)
        self.assertIn("(const unsigned char *)wire", snapshot)


if __name__ == "__main__":
    unittest.main()
