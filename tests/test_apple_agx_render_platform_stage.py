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

    def test_backend_start_persists_exact_portable_result(self):
        header = (RENDER / "include" / "render_admission.h").read_text()
        receipts = (RENDER / "src" / "receipts.c").read_text()
        platform = (RENDER / "src" / "backend_platform_windows.c").read_text()
        self.assertIn("AdmissionRecordBackendStartResult", header)
        self.assertIn('L"Wom1BackendStartResult"', receipts)
        self.assertIn("APPLE_AGX_BACKEND_RUNTIME_RESULT backend_result", platform)
        self.assertIn(
            "AdmissionRecordBackendStartResult(Context, backend_result)", platform
        )

    def test_firmware_start_persists_provider_phase_result_and_mask(self):
        header = (RENDER / "include" / "render_admission.h").read_text()
        receipts = (RENDER / "src" / "receipts.c").read_text()
        platform = (RENDER / "src" / "backend_platform_windows.c").read_text()
        self.assertIn('L"Wom1FirmwarePhase"', receipts)
        self.assertIn('L"Wom1FirmwareResult"', receipts)
        self.assertIn('L"Wom1FirmwareCompletedMask"', receipts)
        self.assertIn('L"Wom1FirmwareFailureResult"', receipts)
        self.assertIn('L"Wom1FirmwareFailureMask"', receipts)
        self.assertIn("runtime->FirmwareIo.RecordPhase = AdmissionFirmwareRecordPhase", platform)
        self.assertIn("AdmissionRecordFirmwarePowerOn", header)
        for receipt in (
            'L"Wom1FirmwarePowerAcquire"',
            'L"Wom1FirmwarePowerState"',
            'L"Wom1FirmwarePowerResult"',
            'L"Wom1FirmwarePowerReceiptSequence"',
        ):
            self.assertIn(receipt, receipts)
        power_on = platform[
            platform.index("AdmissionFirmwarePowerOn("):
            platform.index("AdmissionFirmwarePowerOff(")
        ]
        self.assertLess(power_on.index("AdmissionRecordFirmwarePowerOn"),
                        power_on.index("if (!acquired)"))

    def test_rtkit_boot_failure_is_durable_before_firmware_boolean_folding(self):
        header = (RENDER / "include" / "render_admission.h").read_text()
        receipts = (RENDER / "src" / "receipts.c").read_text()
        platform = (RENDER / "src" / "backend_platform_windows.c").read_text()
        self.assertIn("AdmissionRecordRtkitBoot", header)
        for value in ("Wom1RtkitBootResult", "Wom1RtkitBootPhase",
                      "Wom1RtkitCpuReady", "Wom1RtkitHelloSeen",
                      "Wom1RtkitInboxControl", "Wom1RtkitOutboxControl"):
            self.assertIn(value, receipts)
        boot = platform[platform.index("AdmissionFirmwareBootAsc("):
                        platform.index("AdmissionFirmwareStopAsc(")]
        self.assertLess(boot.index("AdmissionRecordRtkitBoot"),
                        boot.index("return result =="))

    def test_pre_asc_sgx_preparation_matches_current_m1n1_poke(self):
        platform = (RENDER / "src" / "backend_platform_windows.c").read_text()
        power = platform[platform.index("AdmissionFirmwarePowerOn("):
                         platform.index("AdmissionFirmwarePowerOff(")]
        self.assertIn("ADMISSION_PLATFORM_SGX_PRE_ASC_OFFSET", platform)
        self.assertIn("0xd14000u", platform)
        self.assertIn("0x00070001u", platform)
        self.assertLess(power.index("READ_REGISTER_ULONG"),
                        power.index("WRITE_REGISTER_ULONG"))
        self.assertLess(power.index("WRITE_REGISTER_ULONG"),
                        power.index("runtime->Powered = TRUE"))


if __name__ == "__main__":
    unittest.main()
