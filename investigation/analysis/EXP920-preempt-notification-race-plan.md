# EXP920: concurrent notification attempts must not fault a healthy adapter

WHY THIS HYPOTHESIS:
1. EXP919 kernel f5827798, exact918PDB, provesCPU5/System119/2/80000011 at38.769s.
   Normal paging735 is rejected with SchedulerFaulted1 although CPUqueue0,
   SchedulerQueue0, Active0, Dispatched0, PagingPending0, PagingStopping0.
   Completed731/LastSubmitted732, PreemptedFenceQueue[0]=732, phaseIdle/pending0.
2. RenderPacketEmpty, BackendImageReady1 with no bound/job fence, backendReady,
   WorkScheduled0/WorkersActive0/Stopping0/Resetting0 support a completed queued
   preemption, not a presently busy engine. Backend TerminalResultInvalidState
   with TerminalPending0 is the normal ClearPending default, not proof of GPU error.
3. Real PreemptCommand caches notifyNow under locks then unlocks. WorkerFinished
   or the DPC can claim/notify/commit before that cached decision is used.
   TryNotify then fails to claim an already completed/owned notification, returns
   FALSE, and PreemptCommand sets SchedulerFaulted. Actual production-code replay
   is RED with exactly phaseIdle/fault1/one notification/731/732.

WINDOWS CONTRACT: FULL GRAPHICS. Driver-owned notification synchronization must
   tolerate competing legitimate callers without publishing duplicate
   DMA_PREEMPTED or poisoning the adapter. SynchronizeExecution/NotifyInterrupt/
   QueueDpc and the existing deferred worker retirement ordering remain unchanged.
   A well-formed SubmitCommandVirtual may not return DEVICE_BUSY. The dump's
   secondary refusal is proven; the first fault-setting source was not recorded.
AGX/ASAHI CONTRACT: no active GPU work in the observed state. Queued packet732 was
   discarded at preemption, storage/submission ownership released, worker retired.
   No MMIO/UAT/firmware/power/graphics resource change; b5291548 remains validated.
TRANSLATION: under SchedulerLock, a notification attempt seeing Idle, Claimed by
   another caller, or a new WaitCurrentBoundary is accepted as a benign no-op/
   deferral. A ReadyToNotify boundary must still be claimed and delivered exactly
   once. Unrecognized phase, invalid interface and real synchronization/commit
   failures remain failures. Preserve WorkScheduled deferral and postcommit wake.
WHAT IS STILL UNKNOWN: the dump did not capture which site first set Faulted1.
   The reproduced race is confirmed as a source defect and matches the observed
   state, but must not be called the uniquely proven original interleaving.
   Add diagnostic first-fault origin tags while preserving nonzero-fault Boolean
   semantics to make a subsequent failure distinguishable. Observe real hardware
   after the fix; no DWM reinitialization in this crash-validation run.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Run actual PreemptCommand/TryNotify/WorkerFinished with the real shared scheduler.
Inject a competing notifier exactly after PreemptCommand releases its final lock.
One notification must be delivered, phase becomesIdle, no fault is latched, and
new paging735 remains admissible. Also test a concurrent attempt while Claimed,
normal notification, and preservation of genuine callback/unrecognized-phase failure.
This is not a test of a reimplemented scheduler or an invented protocol failure.

Diagnostic tags: SchedulerFaulted stays a LONG with identical zero/nonzero
behavior. The first transition0->nonzero stores file-prefix|source-line, using
InterlockedCompareExchange. Reset0 clears the tag atomically as before. External
Boolean receipt fields are normalized to0/1. No new advertised capability or
public structure size. Exact source commit/PDB identifies the line. Unknown1
remains a valid nonzero fault. This is observation, not another behavior fix.

Inspected: EXP919 full dump/PDB args/object/device/scheduler/runtime; current
scheduler_windows.c TryNotify/PreemptCommand/Dpc/WorkerFinished; work_queue_windows.c
admission; shared apple_agx_scheduler.c claim/commit; shared backend ClearPending.
Microsoft SubmitCommandVirtual, GPU preemption, SynchronizeExecution, NotifyInterrupt
and QueueDpc documentation from the existing notification contract applies.
No historical comparison or lower-layer rewrite. Original/recovery evidence must
be host verified and exact918 removed before920 stage; ordinary377/392 recovery,
hidden385 emergency only. Desktop remains FAIL; DWM test919 did not execute.
