from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"


class AppleAgxSubmitRenderGuardTests(unittest.TestCase):
    def test_submit_render_emits_exact_dispatch_safe_guard_and_status(self):
        header = (RENDER / "include" / "render_admission.h").read_text()
        guards = (RENDER / "include" / "render_submit_trace.h").read_text()
        trace = (RENDER / "src" / "submit_trace_windows.c").read_text()
        submit = (RENDER / "src" / "submission_windows.c").read_text()

        self.assertIn("AdmissionSubmitRenderGuardWindows", header)
        self.assertIn("ADMISSION_SUBMIT_RENDER_GUARD_TAG", guards)
        self.assertIn("AdmissionSubmitRenderGuardWord", guards)
        self.assertIn("ADMISSION_SUBMIT_FENCE_DETAIL_TAG", guards)
        self.assertIn("AdmissionSubmitFenceDetailWindows", header)
        self.assertIn("AdmissionSubmitFenceDetailWindows", trace)
        self.assertIn("AdmissionSubmitRenderGuardWindows", trace)
        self.assertIn("J313_AGX_G2_POWER_CMD_QUERY", trace)
        helper = trace.split(
            "_Use_decl_annotations_ VOID AdmissionSubmitRenderGuardWindows", 1
        )[1].split(
            "_Use_decl_annotations_ VOID AdmissionSubmitFenceDetailWindows", 1
        )[0]
        self.assertEqual(helper.count("WRITE_REGISTER_ULONG64"), 1)
        self.assertEqual(helper.count("WRITE_REGISTER_ULONG(command"), 1)
        fence_helper = trace.split(
            "_Use_decl_annotations_ VOID AdmissionSubmitFenceDetailWindows", 1
        )[1].split(
            "_Use_decl_annotations_ VOID AdmissionPatchRenderGuardWindows", 1
        )[0]
        self.assertEqual(fence_helper.count("WRITE_REGISTER_ULONG64"), 1)
        self.assertEqual(fence_helper.count("WRITE_REGISTER_ULONG(command"), 1)
        self.assertIn("ADMISSION_SUBMIT_RENDER_GUARD", guards)
        self.assertIn("AdmissionSubmitRenderGuardContextMagic", submit)
        self.assertIn("AdmissionSubmitRenderGuardContextDevice", submit)
        self.assertIn("AdmissionSubmitRenderGuardAdapter", submit)
        self.assertIn("AdmissionSubmitRenderGuardSystem", submit)
        self.assertIn("AdmissionSubmitRenderGuardSchedulerInactive", submit)
        self.assertIn("AdmissionSubmitRenderGuardNode", submit)
        self.assertIn("AdmissionSubmitRenderGuardEngine", submit)
        self.assertIn("AdmissionSubmitRenderGuardFence", submit)
        self.assertIn(
            "AdmissionSubmitFenceDetailWindows(Context,\n"
            "        render_context->Object.FenceOutstanding,\n"
            "        Args->SubmissionFenceId)",
            submit,
        )
        self.assertIn(
            "Context, AdmissionSubmitRenderGuardAccepted, STATUS_SUCCESS",
            submit,
        )
        self.assertIn("AdmissionSubmitPacketGuardWindows", submit)
        for name in (
            "AdmissionSubmitPacketGuardState",
            "AdmissionSubmitPacketGuardFence",
            "AdmissionSubmitPacketGuardContext",
            "AdmissionSubmitPacketGuardPrivate",
            "AdmissionSubmitPacketGuardPrivateEnd",
            "AdmissionSubmitPacketGuardDmaStart",
            "AdmissionSubmitPacketGuardDmaEnd",
            "AdmissionSubmitPacketGuardBind",
            "AdmissionSubmitPacketGuardScheduler",
            "AdmissionSubmitPacketGuardQueue",
        ):
            self.assertIn(name, submit)
        self.assertNotIn(
            "AdmissionSubmitRenderGuardWindows(Context, MAXULONG, STATUS_PENDING)",
            submit,
        )

    def test_production_build_has_no_registry_receipt_calls(self):
        header = (RENDER / "include" / "render_admission.h").read_text()
        submit = (RENDER / "src" / "submission_windows.c").read_text()
        trace = (RENDER / "src" / "submit_trace_windows.c").read_text()
        self.assertIn("#define AdmissionSubmitRenderGuardWindows", header)
        self.assertNotIn("AdmissionRecordSubmitRenderGuard", submit)
        self.assertNotIn("IoOpenDeviceRegistryKey", trace)

    def test_rejected_packet_is_correlated_before_broker_and_fatal_return(self):
        submit = (RENDER / "src" / "submission_windows.c").read_text()
        rejected = submit.split("if (!accepted) {", 1)[1].split(
            "AdmissionGdiReceiptSubmitWindows(Context, Args, STATUS_SUCCESS)", 1
        )[0]

        correlation = rejected.index("ADMISSION_CORRELATE_SUBMIT_EXIT")
        broker = rejected.index("AdmissionSubmitPacketGuardWindows")
        fatal_return = rejected.index("return STATUS_DEVICE_BUSY")
        self.assertLess(correlation, broker)
        self.assertLess(broker, fatal_return)
        self.assertNotIn("KeStallExecutionProcessor", rejected)
        self.assertNotIn("ZwFlushKey", rejected)


if __name__ == "__main__":
    unittest.main()
