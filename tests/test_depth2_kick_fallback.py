"""Depth-2 kick fallback wiring (investigation/analysis/DEPTH2-KICK-FALLBACK.md).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A fallback that loses a job
the firmware may hold (S4 after SubmitSuccessor, S5 adoption) or that leaves
the scheduler faulted / the G4 head Kicked; the real state machine is proved
in render_job_window_rc102_model.c (s_fallback), this pins the KMD wiring.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
WORKER = ROOT / "drivers/apple-agx/render-admission/src/backend_platform_windows.c"
G3 = ROOT / "drivers/apple-agx/render-admission/src/gpuva_g3_windows.c"


def section(text, head):
    start = text.index(head)
    end = text.index("\n}\n", start)
    return text[start:end + 2]


class Depth2KickFallback(unittest.TestCase):
    def test_fallback_never_faults_and_unkicks_under_both_locks(self):
        src = WORKER.read_text()
        fb = section(src, "static BOOLEAN AdmissionJobWindowKickFallbackWindows(")
        self.assertNotIn("SchedulerFaulted, 0x", fb)
        lock = fb.index("KeAcquireSpinLock(&adapter->PagingLock")
        sched = fb.index("KeAcquireSpinLockAtDpcLevel(&adapter->SchedulerLock)")
        unkick = fb.index("AdmissionG4PendingUnkickLocked(adapter, fence)")
        self.assertLess(lock, sched)
        self.assertLess(sched, unkick)
        self.assertLess(unkick, fb.index("KeReleaseSpinLockFromDpcLevel(&adapter->SchedulerLock)"))
        self.assertIn("runtime->Backend.SuccessorPending", fb)
        self.assertIn("ADMISSION_KICK_FAIL_FELL_BACK", fb)
        # EXP2222: the preemption branch must be observable on hardware; the
        # phase is read before the unkick changes it.
        self.assertLess(fb.index("AppleAgxSchedulerPreemptionPhase(&adapter->Scheduler)"),
                        fb.index("AdmissionG4PendingUnkickLocked(adapter, fence)"))
        self.assertIn("++runtime->Depth2Receipt.KickFallbacksPreempt;", fb)

    def test_only_pre_visibility_refusals_fall_back(self):
        kick = section(WORKER.read_text(), "static BOOLEAN AdmissionJobWindowKickWindows(")
        submit = kick.index("result = AppleAgxBackendRuntimeSubmitSuccessor(")
        # C1 adds one before SubmitSuccessor: the cross-process slot re-proof.
        self.assertEqual(kick.count("AdmissionJobWindowKickFallbackWindows("), 5)
        self.assertEqual(kick[:submit].count("AdmissionJobWindowKickFallbackWindows("), 4)
        # S4a: the only fallback after SubmitSuccessor; QueueFailed (RunSuccessor
        # was called: S4b/S4c may be visible) and a build not given back reset.
        after = kick[submit:]
        self.assertEqual(after.count("AdmissionJobWindowKickFallbackWindows("), 1)
        s4 = after[after.index("if (result != AppleAgxBackendRuntimeResultQueueFailed &&"):]
        s4 = s4[:s4.index("return TRUE;")]
        self.assertIn("(flags & ADMISSION_KICK_FAIL_G3_ABORTED) != 0u", s4)
        self.assertLess(s4.index("AdmissionJobWindowKickBuildUndoneWindows(runtime, image, owner, next)"),
                        s4.index("AdmissionJobWindowKickFallbackWindows("))
        self.assertIn("ADMISSION_KICK_FAIL_BUILD_UNDONE", s4)
        # The rollback (flight cancel, manager cancel, G3 abort, unstage) runs
        # first; the fail-closed reset stays the default after it.
        self.assertLess(after.index("AdmissionBackendImageUnstageJob("),
                        after.index("AdmissionJobWindowKickBuildUndoneWindows("))
        self.assertLess(after.index("AdmissionJobWindowKickBuildUndoneWindows("),
                        after.index("AdmissionJobWindowKickFailedWindows("))
        adopt = section(WORKER.read_text(), "static BOOLEAN AdmissionJobWindowAdoptWindows(")
        self.assertNotIn("KickFallback", adopt)

    def test_build_undone_requires_every_built_piece_given_back(self):
        """S4a: a flight, manager transaction, staged sequence or overlay left
        for the fence would make the depth-one rebuild count twice, be refused,
        or refuse the running job's release (overlay state fence mismatch)."""
        undone = section(WORKER.read_text(),
                         "static BOOLEAN AdmissionJobWindowKickBuildUndoneWindows(")
        for check in ("!owner->NotifierFlight",
                      "owner->Flights[index].Fence == fence",
                      "owner->ManagerFence == fence || owner->ManagerSecondFence == fence",
                      "image->JobReady && image->JobFence == fence",
                      "AdmissionDynamicOverlayReleaseActive(runtime, fence)"):
            self.assertIn(check, undone)
        self.assertNotIn("SchedulerFaulted", undone)

    def test_built_drill_refuses_only_kicked_successors(self):
        relocate = section(WORKER.read_text(), "static APPLE_AGX_BACKEND_BOOL AdmissionBackendRelocate(")
        drill = relocate[relocate.index("runtime->Depth2FallbackDrillBuilt != 0u"):]
        self.assertIn("runtime->Depth2KickedFence == Submission->Submission.Fence", drill)
        self.assertIn('L"Wom1Depth2FallbackDrillBuilt"', WORKER.read_text())

    def test_unkick_returns_the_head_to_prebound(self):
        unkick = section(G3.read_text(), "_Use_decl_annotations_ BOOLEAN AdmissionG4PendingUnkickLocked(")
        self.assertIn("entry->State != AdmissionG4PendingKicked", unkick)
        self.assertIn("AppleAgxSchedulerUnkickFence(&adapter->Scheduler, 0u, 0u, fence)", unkick)
        self.assertIn("entry->State = AdmissionG4PendingPrebound;", unkick)
        # EXP2221: during a boundary preemption the fence goes back to VidSch;
        # the entry is dropped like DropLocked drops a Prebound one.
        self.assertLess(unkick.index("AppleAgxSchedulerPreemptionPhase(&adapter->Scheduler)"),
                        unkick.index("AppleAgxSchedulerUnkickFence("))
        self.assertIn("AdmissionGpuvaG3PrivatePreempt(context, fence);", unkick)
        self.assertIn("AdmissionContextRetireFence(context, fence);", unkick)
        self.assertIn("entry->State = AdmissionG4PendingDropped;", unkick)


if __name__ == "__main__":
    unittest.main()
