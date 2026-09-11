from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"


class AppleAgxRenderMemoryQualificationTests(unittest.TestCase):
    def read(self, relative):
        return (RENDER / relative).read_text()

    def test_opt_in_profile_reuses_production_memory_start_and_stop(self):
        project = self.read("AppleAgxRenderAdmission.vcxproj")
        script = self.read("scripts/build-driver.ps1")
        lifecycle = self.read("src/lifecycle.c")

        self.assertIn("APPLE_AGX_RENDER_MEMORY_QUALIFICATION=1", project)
        self.assertIn("AppleAgxMemoryQualification", project)
        self.assertIn("[switch]$MemoryQualification", script)
        self.assertIn("AdmissionMemoryRuntimeStart(context)", lifecycle)
        self.assertIn("context->MemoryStartStage", lifecycle)
        self.assertIn("qualification.StartStage", lifecycle)
        self.assertIn("AdmissionMemoryRuntimeQualify(context", lifecycle)
        self.assertIn("AdmissionMemoryRuntimeStop(context)", lifecycle)
        self.assertIn("#if defined(APPLE_AGX_RENDER_MEMORY_QUALIFICATION)",
                      lifecycle)
        self.assertNotIn("__hvc", lifecycle)

    def test_proof_reads_the_actual_context63_uat_and_hvc_telemetry(self):
        runtime = self.read("src/memory_runtime_windows.c")
        physical = self.read("src/physical_memory_windows.c")
        receipts = self.read("src/receipts.c")

        for token in (
            "AdmissionMemoryRuntimeQualify",
            "AppleAgxUatResolvePage",
            "ADMISSION_MEMORY_UAT_CONTEXT",
            "Published.PublishedTtbr0",
            "Published.PublishedTtbr1",
            "LastHvcReturnStatus",
            "LastHvcPayloadStatus",
            "HvcInvocationCount",
            "TranslatedPageCount",
            "AdmissionMemoryStartPhysicalOwner",
            "AdmissionMemoryStartLocalObject",
            "AdmissionMemoryStartResidency",
            "AdmissionMemoryStartMapping",
            "AdmissionMemoryStartPublication",
            "AdmissionMemoryStartComplete",
            "J313_AGX_G2_POWER_REG_REQUEST_SEQUENCE",
            "J313_AGX_G2_POWER_REG_COMMAND",
            "J313_AGX_G2_POWER_CMD_QUERY",
            "WRITE_REGISTER_ULONG64",
        ):
            self.assertIn(token, runtime + physical)
        self.assertIn("AdmissionRecordMemoryQualification", receipts)
        self.assertIn("REG_BINARY", receipts)

    def test_profile_exits_before_scheduler_display_and_agx_backend(self):
        lifecycle = self.read("src/lifecycle.c")
        qualify = lifecycle.index("status = AdmissionMemoryRuntimeQualify")
        start = lifecycle.rfind(
            "#if defined(APPLE_AGX_RENDER_MEMORY_QUALIFICATION)", 0, qualify
        )
        end = lifecycle.index("#endif", start)
        branch = lifecycle[start:end]
        self.assertIn("return STATUS_NOT_SUPPORTED", branch)
        self.assertNotIn("AdmissionSchedulerStart", branch)
        self.assertNotIn("DxgkCbAcquirePostDisplayOwnership", branch)
        for forbidden in ("RTKit", "queue", "TA+3D", "880", "881"):
            self.assertNotIn(forbidden, branch)


if __name__ == "__main__":
    unittest.main()
