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
        self.assertEqual(kick.count("AdmissionJobWindowKickFallbackWindows("), 3)
        self.assertLess(kick.rindex("AdmissionJobWindowKickFallbackWindows("), submit)
        # S4: a refused SubmitSuccessor stays fail-closed until its builder
        # rollback is proved (DEPTH2-KICK-FALLBACK.md S4a/S4b).
        self.assertIn("AdmissionJobWindowKickFailedWindows(", kick[submit:])
        adopt = section(WORKER.read_text(), "static BOOLEAN AdmissionJobWindowAdoptWindows(")
        self.assertNotIn("KickFallback", adopt)

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
