# EXP914: retire the cancelled render worker before publishing preemption

WHY THIS HYPOTHESIS:
1. EXP913 actual DWM1248 captures three first-stage failures (fences3923,8530,10554; prior private/preempt3922,8529,10553) with outerPredicate13/runtimePredicate8. BackendReady, provider/start/workitem, stop/reset and context guards all passed before the failing WorkScheduled test. This is direct hardware evidence of temporary worker ownership being misclassified as malformed DMA.
2. Current AdmissionDdiPreemptCommand discards a queued packet, releases its image/private ownership and notifies Windows immediately when no active GPU fence remains, while the IO work-item reservation can still be pending. Windows then submits the valid new resubmission fence while WorkScheduled remains1.
3. Actual production notify/worker functions linked with the real scheduler reproduce DMA_PREEMPTED while WorkScheduled1 (RED). Merely dropping the admission check is unsafe: a delayed worker can encounter the replacement packet in Prepared state and fault it before binding/queueing finishes.

WINDOWS CONTRACT:
FULL GRAPHICS WDDM3.0 pinnedWDK26100. GPU preemption resubmits nonpaging commands with new fences. SubmitCommandVirtual INVALID_PARAMETER means malformed data and poisons the device; it is not ordinary queue backpressure. PreemptCommand is nonpageable atDISPATCH, reports through synchronized interrupt+QueueDpc. DxgkCbSynchronizeExecution accepts <=DISPATCH, so the existing notification path can be retried from worker retirement. Sources: Microsoft GPU preemption; DXGKDDI_SUBMITCOMMANDVIRTUAL; DXGKDDI_PREEMPTCOMMAND; DXGKCB_SYNCHRONIZE_EXECUTION (all official pages checked).

AGX/ASAHI CONTRACT:
No AGX packet, RTKit, UAT, power, IRQ, m1n1 or Mu change. The queue-discard case has no active GPU work. The host KMD work-item reservation is the remaining owner and must retire before its render slot is offered back to Windows. Preserve actual hardware completion and preemption fence values; never claim discarded render work completed. Inherited pinnedAsahi77cb8f24/m1n19320da31/Mu0dac6871 ownership remains as recorded in EXP913.

TRANSLATION:
Central TryNotifyPreemption defers while the render work-item reservation exists, under the scheduler lock. It does not claim/commit a preemption during deferral and returns accepted/deferred to the DDI rather than faulting. WorkerFinished clears WorkScheduled under that same lock, retries pending eligible preemption outside the lock through existing synchronized notify, then dispatches queued work. Retry respects DispatchedFence and unreported CPU paging completions; existing completion DPC remains responsible if those are pending. Stopping/resetting skip the retry. Keep strict envelope RuntimeReady and dispatch guards.

ATOMIC CONTRACT:
Deferral while WorkScheduled is held plus retry immediately after its release are one ownership handoff invariant. Deferral alone can strand preemption; retry alone leaves premature notification. These two sites implement the documented OS resubmission boundary. No independent capabilities or scheduling policies are bundled.

WHAT IS STILL UNKNOWN:
Whether clearing this confirmed DWM admission defect allows normal display Present and nonzero/updating DCP scanout, or exposes another downstream boundary. Hardware tests the implemented mapping, not an undocumented capability guess.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Actual notify/retirement replay must never publish DMA_PREEMPTED while a cancelled render worker can still run. It must publish exactly once after retirement, preserve completed fence0, admit nextnewfence, defer behind unreported CPU/render completion, and avoid retry on stop/reset. Existing worker lifetime, paging replay and actual G4 strict envelope tests remain relevant.

Inspected source: scheduler_windows.c TryNotify/DdiPreempt/Dpc; backend_platform_windows.c Worker entry/Finished/RuntimeReady; work_queue_windows.c dispatch reservations; render_submission.c Prepared/Queued state; shared apple_agx_scheduler.c boundary preemption. Smallest checkpoint: no DWM Predicate13/8 on valid nonpaging resubmission after observed preemption, natural subsequent queue completion, then real standardPresent/currentDCP. Recovery immutable377/392 visible exactpackage cleanup after independently verified evidence; hidden385 only emergency. Finish EXP913 original750s and recovery before installing EXP914.
