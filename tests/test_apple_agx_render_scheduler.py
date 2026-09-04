from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"


class AppleAgxRenderSchedulerTests(unittest.TestCase):
    def read(self, relative):
        return (RENDER / relative).read_text()

    def test_one_node_scheduler_is_in_adapter_and_context_lifetimes(self):
        header = self.read("include/render_admission.h")
        callbacks = self.read("src/callbacks.c")
        lifecycle = self.read("src/lifecycle.c")

        self.assertIn("APPLE_AGX_SCHEDULER Scheduler", header)
        self.assertIn("APPLE_AGX_SCHEDULER_CONTEXT SchedulerContext", header)
        self.assertIn("AdmissionSchedulerStart(context)", lifecycle)
        self.assertIn("AdmissionSchedulerStop(context)", lifecycle)
        self.assertIn("AppleAgxSchedulerCreateContext", callbacks)
        self.assertIn("AppleAgxSchedulerDestroyContext", callbacks)

    def test_paging_completion_advances_the_single_engine_fence(self):
        paging = self.read("src/paging_windows.c")
        scheduler = self.read("src/scheduler_windows.c")

        self.assertIn("AdmissionSchedulerRecordCompletion", paging)
        self.assertIn("AppleAgxSchedulerCompleteFence", scheduler)
        self.assertIn("AdmissionDdiQueryCurrentFence", scheduler)
        self.assertNotIn("FAIL2(AdmissionDdiQueryCurrentFence", self.read(
            "src/callbacks.c"))

    def test_preemption_and_tdr_are_wired_but_fail_closed_on_active_work(self):
        scheduler = self.read("src/scheduler_windows.c")
        callbacks = self.read("src/callbacks.c")

        for token in (
            "AppleAgxSchedulerBeginBoundaryPreemption",
            "DXGK_INTERRUPT_DMA_PREEMPTED",
            "AppleAgxSchedulerClaimBoundaryPreemption",
            "AppleAgxSchedulerCommitBoundaryPreemption",
            "AdmissionDdiQueryDependentEngineGroup",
            "AdmissionDdiQueryEngineStatus",
            "AdmissionDdiResetEngine",
            "AdmissionDdiResetFromTimeout",
            "AdmissionDdiRestartFromTimeout",
            "AdmissionDdiCollectDbgInfo",
        ):
            self.assertIn(token, scheduler)
        for name in (
            "AdmissionDdiPreemptCommand",
            "AdmissionDdiQueryCurrentFence",
            "AdmissionDdiQueryDependentEngineGroup",
            "AdmissionDdiQueryEngineStatus",
            "AdmissionDdiResetEngine",
            "AdmissionDdiResetFromTimeout",
            "AdmissionDdiRestartFromTimeout",
            "AdmissionDdiCollectDbgInfo",
        ):
            self.assertNotIn(f"FAIL2({name}", callbacks)
        self.assertIn("PagingPending", scheduler)
        self.assertIn("STATUS_DEVICE_BUSY", scheduler)

    def test_project_links_only_the_pure_scheduler_primitive(self):
        project = self.read("AppleAgxRenderAdmission.vcxproj")
        self.assertIn(r"src\scheduler_windows.c", project)
        self.assertIn(r"..\shared\src\apple_agx_scheduler.c", project)
        self.assertNotIn(r"..\shared\src\apple_agx_submission.c", project)
        self.assertNotIn(r"..\shared\src\apple_agx_recovery.c", project)


if __name__ == "__main__":
    unittest.main()
