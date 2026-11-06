# Depth-2 kick fallback: a refused kick becomes depth 1, never an engine reset

Status: offline design note (2026-11-06). No source, test or hardware change.
Classification: **C** (contract correctness, GPU_DRIVER_ARCHITECTURE.md 2.1
"BUILD a refused depth-2 kick must fall back to depth one").
Trigger evidence: EXP2213 (EXPERIMENTS.md line ~70167): all 5 OV5-waived kicks
were refused by the backend runtime's `AppleAgxBackendSuccessorLeadValid`
guard (ImageFailed, nothing published) and every one became an ENGINE reset,
because every post-decision failure calls
`AdmissionJobWindowKickFailedWindows`, which sets `adapter->SchedulerFaulted`
(`0x40000 | line`).

Sources inspected (worktree `integration-ad04-windows-compiler`):

- `drivers/apple-agx/render-admission/src/backend_platform_windows.c`
  `AdmissionJobWindowKickFailedWindows` (6208), `AdmissionJobWindowKickWindows`
  (6318-6603), `AdmissionJobWindowAdoptWindows` (6608), `AdmissionJobWindowNextWindows`
  (6636), worker poll loop (7085-7095), completion/chain tail (7475-7497),
  `AdmissionJobAheadChainWindows` (6143), `AdmissionBackendComplete` (5085-5100,
  5365-5372), retire of a kicked entry (5630-5645), `AdmissionBackendRelocate`
  (4840), `AdmissionCancelUnpublishedManager` (3813).
- `drivers/apple-agx/render-admission/src/gpuva_g3_windows.c`
  `AdmissionG4PendingAheadPromoteLocked` (5600), `AdmissionG4PendingKickLocked`
  (5638), `AdmissionG4PendingAdoptLocked` (5661), `AdmissionG4PendingAbortKickedLocked`
  (5701), `AdmissionG4PendingAheadUnbind` (5737), `AdmissionGpuvaG3BeginJobAs`
  successor branch (4150-4225), `AdmissionGpuvaG3BeginSuccessorJob` (4427),
  `AdmissionGpuvaG3AbortJob` (4616).
- `drivers/apple-agx/render-admission/include/render_admission.h` (G4 pending
  states, `AdmissionG4PendingKicked = 6`).
- `drivers/apple-agx/shared/src/apple_agx_scheduler.c` (`KickQueuedFence` 331,
  `ActivateFence` 310, `BeginBoundaryPreemption`/`TakeKicked` 458-515,
  `ResetEngine` 645-665).
- `drivers/apple-agx/shared/src/apple_agx_backend_runtime.c`
  `AppleAgxBackendRuntimeSubmitSuccessor` (240-293).
- `drivers/apple-agx/shared/src/apple_agx_g13_queue_provider.c`
  `AppleAgxG13ProviderRunSuccessor` (756), `SubmitRender` failure tail (590-600).
- `drivers/apple-agx/shared/src/apple_agx_g13_queue_runtime.c`
  `AppleAgxG13SuccessorFailure` (144), `AppleAgxG13QueueRuntimeSubmit` (541-680).
- `drivers/apple-agx/shared/src/apple_agx_platform_provider.c`
  `PreparePublish`/`FinishPublish` (433-446), render Relocate (520-545).
- `drivers/apple-agx/shared/src/apple_agx_render_shared_memory.c`
  `AppleAgxRenderFlightPublish`/`Cancel` (680-730), job-word set (1215-1218, 1328).
- `drivers/apple-agx/shared/src/apple_agx_gpuva_g3_graph.c`
  `BeginNestedJob`/`EndJob` (1138-1160), `ReleaseRetainedAt` (181).
- `.local/reference/wdk26100/d3dkmddi.h` (DXGKARGCB_NOTIFY_INTERRUPT_DATA 1003-1021,
  DXGKARG_SUBMITCOMMANDVIRTUAL 5218, DXGKARG_RESETENGINE 8243-8247).
- `.local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi/workqueue.rs`
  (`Job::commit` 358, `JobSubmission::run` 501, `Drop for Job` 542-600).
- tests: `tests/render_job_window_rc102_model.c` + `tests/test_render_job_window.py`,
  `tests/test_render_job_depth2_kick_rc102.py`, `tests/test_render_job_depth2_exp2047.py`.

---

## WINDOWS CONTRACT

- Fence B was accepted by `DxgkDdiSubmitCommandVirtual`
  (`DXGKARG_SUBMITCOMMANDVIRTUAL.SubmissionFenceId`, d3dkmddi.h 5218-5232).
  From that point dxgkrnl requires exactly one of: a
  `DXGK_INTERRUPT_DMA_COMPLETED` with `DmaCompleted.SubmissionFenceId >= B`
  reported in submission order (d3dkmddi.h 1009-1013); inclusion in a
  `DXGK_INTERRUPT_DMA_PREEMPTED` boundary (`PreemptionFenceId`,
  `LastCompletedFenceId`, 1014-1020) so VidSch resubmits it; or a
  TDR / `DxgkDdiResetEngine` whose `LastAbortedFenceId` (8246) covers it.
- WDDM does not specify *when* the KMD hands a submitted packet to the GPU;
  only completion order, preemption honesty and the TDR timeout (~2 s) are
  observable. A packet the KMD internally retries later is indistinguishable
  from one the KMD simply had not started yet, provided its completion comes
  after A's and within the timeout.
- Therefore a never-started B needs no Windows-visible action at all. An
  engine reset for it is a contract-legal but gratuitous outcome (it aborts A
  too, EXP2213), and it is not what Microsoft expects for a driver-internal
  refusal: ResetEngine is for a hung engine.
- Preemption interaction: while B is kicked, the scheduler's boundary logic
  treats B as "with the hardware" (`BeginBoundaryPreemption` keeps the kicked
  fence as the only queued fence and the boundary waits for it, scheduler.c
  503-515). If B silently becomes un-kicked after such a boundary began,
  dxgkrnl would wait for a completion/preemption that no one delivers. So the
  fallback is legal only when no boundary preemption is in progress (or the
  fallback must also re-express B as preempted; see UNKNOWN).

## AGX/ASAHI CONTRACT

- Asahi: firmware visibility of a job is exactly the `cpu_wptr` store plus the
  `RunWorkQueueMsg` in `JobSubmission::run` (workqueue.rs 501-530). A job that
  was built/committed but dropped before `run` is rolled back ("Roll back N
  events ... and N commands", `Drop for Job`, 542-600): events and command
  slots are returned, no GPU recovery, no channel reset. A pre-run failure is
  an ordinary error to the submitter.
- Our queue runtime implements the same boundary: `AppleAgxG13QueueRuntimeSubmit`
  sets `visible = TRUE` only after the first `PublishU32(...CpuWritePointer)`
  (queue_runtime.c 643-648). `AppleAgxG13SuccessorFailure` (144-166) with
  `VisibleToFirmware == FALSE` calls `FinishPublish(FALSE)` (= flight cancel)
  and zeroes `Runtime->Successor`, leaving the running job A untouched;
  with `TRUE` it quiesces and marks the runtime Faulted (A lost too).
- Extra AGX-specific pre-visibility side effect (not in Asahi): `PreparePublish`
  = `AppleAgxRenderFlightPublish` writes the shared notifier threshold word that
  the firmware reads *for the running job A* (render_shared_memory.c 688-701),
  before the write pointer is published. `FlightCancel` restores it
  (720-726) but A's firmware could observe the transient value. A failure after
  `PreparePublish` is "not visible as work" yet not "untouched".
- Builder side effects of a successor build (all KMD memory, none
  firmware-visible): notifier counts, `JobWordSet/JobWordStamp`,
  `ComputeQueueStamp` (restored by `AppleAgxRenderFlightCancel` only when
  `Owner->NotifierFlight` is on; without flights Cancel returns TRUE and
  restores nothing), manager build (`AppleAgxRenderManagerCancelBuild`),
  image stage (`AdmissionBackendImageUnstageJob`: Sequence/Dynamic/Job),
  dynamic overlay (released only on the build-callback failure path, 4665-4672).
- G3: `BeginSuccessorJob` joins A's lease via `GraphBeginNestedJob` (sets
  `JobNested`); `AbortJob` -> `GraphEndJob` clears only `JobNested` while A's
  `JobInFlight` remains, and `ReleaseRetainedAt` is a no-op while
  `JobInFlight` (graph.c 186). So `AbortJob(next)` is safe with A running.
  It also clears `Started` on B's private scene and `state->SuccessorFence`.

## TRANSLATION

Windows "submitted, not started" == AGX "built at most, never published"
(no write-pointer store, no Run message, no threshold write). For such a B the
KMD must restore every KMD-side structure to the exact state it had at
"B Prebound behind running A, no kick decided", then let the existing rc90
chain path submit B at depth 1 after A's completion:

A completes -> `AdmissionJobWindowNextWindows` (Depth2KickedFence == 0, no
adopt) -> `AdmissionJobAheadChainWindows(A)` -> `AdmissionG4PendingAheadPromoteLocked(B)`
(requires entry `Prebound`, `G4PrebindFence == B`, `BackendImage.BoundFence == B`,
render packet Empty, scheduler `QueuedFence == B`) -> `goto ChainNext` ->
activate -> `AdmissionGpuvaG3BeginJob` (non-successor) -> build with
`PlanJob` -> `AppleAgxBackendRuntimeSubmit`. If the chain refuses,
`AdmissionG4PendingAheadUnbind` turns `Prebound` back into `Queued` and the
ordinary WorkerFinished -> DPC -> dispatch path binds and runs B. **Both paths
require the entry to be `Prebound`, not `Kicked`**: `AheadUnbind` returns early
and leaves a `Kicked` entry stuck forever (gpuva_g3_windows.c 5748-5752), and
`ActivateFence` would accept a still-kicked B and wait for a completion the
firmware never produces.

Ownership: the worker owns the decision and the rollback (PASSIVE, outside the
locks for G3/build; scheduler + G4 pending under PagingLock -> SchedulerLock
as in the kick). The reset path keeps owning anything possibly visible.

## WHAT IS STILL UNKNOWN

1. Whether `AdmissionBackendImageUnstageJob` + flight cancel + manager cancel
   fully restore the image for a *compute* or *present* successor (only the
   render stage path is unstaged today; compute envelope builds may touch
   other image fields). Needs an offline read of the compute/present builders
   before enabling fallback for those kinds; first cut: render kinds only, or
   a generic "image fields equal to snapshot" check.
2. Dynamic overlay state when `Relocate` succeeded and the backend then
   refused (`AppleAgxBackendSuccessorLeadValid`, the EXP2213 case): the
   overlay is released only inside the build-failure path. Must be verified
   offline (does the depth-1 rebuild of the same fence re-apply idempotently?).
3. Whether a transient notifier threshold write (failure after
   `PreparePublish`) can be observed by A's firmware. Not needed if fallback is
   restricted to failures before `PreparePublish` (proposed).
4. Preemption begun between the kick decision and the failure (microsecond
   window; decision already refuses on preemption, refusal 4). Proposed:
   fall back only when `PreemptionPhase == Idle`, else keep fail-closed.
   Re-expressing B as preempted is possible but not designed here.
5. Timing receipts: `AdmissionJobTimingMarkWindows(B, Worker/Begin*/Backend*)`
   are re-marked by the depth-1 run; whether the timing table keys by fence
   and overwrites cleanly (diagnostic only, no contract impact).

---

## Implementation plan

### 1. Call sites of `AdmissionJobWindowKickFailedWindows`

| # | Line | Failure | Firmware-visible? | Class | Proof state |
|---|------|---------|-------------------|-------|-------------|
| S1 | 6521 | `context == NULL` or no `GpuvaG3Process` after the kick decision | No: nothing called after `KickLocked` | **(a)** | Only `Depth2KickedFence/Barrier`, timing marks, scheduler `KickedFence`, G4 entry `Kicked` changed |
| S2 | 6531 | `AdmissionGpuvaG3BeginSuccessorJob` refused (any `beginRefusal` 1-9 or `STATUS_INVALID_DEVICE_STATE`) | No: BeginJobAs mutates state only when `refusal == 0` (nested job is the last check, 8) | **(a)** | `!NT_SUCCESS(begin)`; `state->SuccessorFence != next`; `Graph.JobNested == 0` |
| S3 | 6543 | manager-needs-idle recheck after BeginJob (EXP2051) | No: nothing built yet; `AbortJob(next)` already called | **(a)** | `ADMISSION_KICK_FAIL_G3_ABORTED`; build step 0; `state->SuccessorFence == 0` |
| S4a | 6592 | `SubmitSuccessor` result != `QueueFailed` (InvalidArgument/InvalidState/Busy/MemoryFailed/ImageFailed) | No: `RunSuccessor` never called (backend_runtime.c 248-279) | **(a)** if builder rollback proven (below) | `result != QueueFailed`, `!Backend.SuccessorPending`, provider/runtime not Faulted, `flags & G3_ABORTED`, render: `flags & UNSTAGED` or not staged |
| S4b | 6592 | `QueueFailed` with provider guard != `Runtime` (provider `SUBMIT_REJECT`) or runtime result InvalidArgument/InvalidState/Busy | No: rejected before `PublishPreparedWork`/`PreparePublish` | **(a)** with same rollback proof | `provider->LastSuccessorGuard != AppleAgxG13SubmitGuardRuntime` or `LastSubmitRuntimeResult` in {InvalidArgument, InvalidState, Busy}; `provider->SuccessorFence == 0`; `Runtime.Successor.Fence == 0` |
| S4c | 6592 | `QueueFailed` with runtime MemoryFailed/TransportFailed/ResetFailed, or any Faulted phase, or `SuccessorPending` | Possibly (write pointer / Run message / threshold) | **(b)** | `visible` may be TRUE; even when FALSE `PreparePublish` may have written the live threshold |
| S4d | 6592 | S4a/S4b but manager `CancelBuild` failed (G3 not aborted), unstage failed, or flight still present | Not visible, but KMD state not restorable | **(b)** | `!(flags & G3_ABORTED)` or rollback check fails |
| S5 | 6623 | `AdmissionJobWindowAdoptWindows`: `AdoptLocked` refused after A completed | **Yes**: `SubmitSuccessor` returned Ok, firmware holds/ran B | **(b)** | `Backend.PendingSubmission.Fence == kicked` (promoted successor) |

Additional global preconditions for any (a) fallback, checked under
PagingLock -> SchedulerLock: `Stopping == Resetting == 0`,
`SchedulerFaulted == 0`, scheduler `PreemptionPhase == Idle`,
`AppleAgxSchedulerKickedFence == next`, scheduler `QueuedFence == next`,
G4 head entry `Kicked` with `Fence == next`, `G4PrebindFence == next`,
`BackendImage.BoundFence == next`. Any mismatch -> keep today's fail-closed
call.

### 2. State to restore (new helper `AdmissionJobWindowKickFallbackWindows`)

Order (PASSIVE, worker):

1. Builder/backend (S4a/S4b only; already partly done today):
   `AppleAgxRenderFlightCancel(owner, next)` - treat FALSE as success only
   when no flight with `Fence == next` remains (the queue runtime's
   `FinishPublish(FALSE)` may already have cancelled it); require
   `owner->NotifierFlight` or no notifier count change; manager
   `AppleAgxRenderManagerCancelBuild` when `ManagerFence/SecondFence == next`;
   `AdmissionBackendImageUnstageJob(image, next, stageSequence, &stageDynamic)`
   when `image->JobReady && image->JobFence == next`, else require
   `image->Sequence == stageSequence`; release the dynamic overlay (UNKNOWN 2);
   `Backend.SuccessorJob/SuccessorSubmission` already zeroed by the runtime.
2. G3: `AdmissionGpuvaG3AbortJob(adapter, next)` (S3/S4 already; S1/S2 none
   needed - verify `state->SuccessorFence != next`).
3. Scheduler + G4, under PagingLock -> SchedulerLock, recheck the global
   preconditions, then:
   - new `AppleAgxSchedulerUnkickFence(sch, 0, 0, next)`: requires
     `KickedFence == next`, `QueuedFence == next`, `ActiveFence != 0`,
     `PreemptionPhase == Idle`; sets `KickedFence = 0`.
   - new `AdmissionG4PendingUnkickLocked(adapter, next)`: requires head entry
     `Kicked`, `Fence == next`, `G4PrebindFence == next`, `BoundFence == next`;
     sets `State = AdmissionG4PendingPrebound` (keeps `G4PrebindFence`, image
     binding and packet). The `GpuvaG3MappingGeneration/DmaBufferVa/Bytes`
     copied by `KickLocked` are rewritten identically by `AheadPromoteLocked`.
4. Runtime: `Depth2KickedFence = 0`, `Depth2KickBarrier = 0`,
   `Depth2KickRunningKind/NextKind = 0`, `Ov2Waive = Ov5Waive = FALSE`,
   `RcCurrent.CFence = 0`; leave `kickDone = TRUE` (no re-kick of B behind A;
   the caller already returns TRUE) and `kickedBehind` stays FALSE because it
   is set only when `Depth2KickedFence != 0` after the call (7092).
5. Receipt only: increment new `Depth2Receipt.KickFallbacks`, record the
   failure struct as today but **without** touching `SchedulerFaulted`; export.

Not touched: `adapter->SchedulerFaulted`, `AdmissionG4PendingAbortKickedLocked`
(that is the reset path's "may have run" handler; it retires the fence, which
would lose B), backend `PendingSubmission` (A), provider `PendingFence` (A).

Depth-1 trigger after A completes: unchanged worker tail (7475-7497).
`AdmissionJobWindowNextWindows` sees `Depth2KickedFence == 0` -> no adopt;
`AdmissionJobAheadChainWindows(A)` promotes the `Prebound` B and chains; if it
refuses, `AdmissionG4PendingAheadUnbind` returns B to `Queued` and the DPC
dispatch path runs it. The un-kicked B is still queued in the scheduler
(`KickQueuedFence` never dequeues; only `ActivateFence`/`TakeKicked` do) and
in `G4Pending` head.

Failure inside the fallback (any check false) -> call the existing
`AdmissionJobWindowKickFailedWindows` (fail closed, reset), never a partial
rollback.

### 3. Smallest offline test

**WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?** A pre-visibility
successor refusal that resets the engine, loses B, reports B twice or before
A, or leaves B `Kicked` so it never runs.

- Extend `tests/render_job_window_rc102_model.c` (driven by
  `tests/test_render_job_window.py::JobWindowRealStateMachine`, real
  scheduler + backend runtime + queue provider/runtime + window): today
  `kick_successor` treats `SubmitSuccessor != Ok` as a violation and sets
  `faulted`. Add a scenario that injects a pre-publication successor refusal
  for one chosen fence (e.g. relocate/lead-valid failure in the model's
  `Image.Relocate`, and a provider-guard rejection), applies the fallback
  (`AppleAgxSchedulerUnkickFence` + model entry back to prebound), and checks:
  no reset, `resets == 0`, `LastAbortedFence` never covers B, B dispatched by
  the depth-1 `dispatch()` after A's report, W1/W5 hold. Add a mutation to
  `test_each_depth2_step_is_load_bearing`: drop the unkick -> the model must
  fail (B activated while still kicked / never ends).
- Add `AppleAgxSchedulerUnkickFence` cases to
  `drivers/apple-agx/shared/tests/apple_agx_scheduler_test.c`
  (`tests/test_apple_agx_scheduler.py`): refused when not kicked, wrong fence,
  no active fence, preemption in progress; accepted otherwise, and
  `ActivateFence(B)` succeeds after `CompleteActiveFence(A)`.
- Existing source-shape tests that pin today's fail-closed order and must be
  updated with the change: `test_render_job_depth2_kick_rc102.py::
  test_refused_submit_ends_the_successor_g3_job` (134) and
  `test_render_job_depth2_exp2047.py::test_refused_kick_unstages_after_ending_the_g3_job`
  (247); add a shape test that S5 (adopt failure) and S4c still call
  `AdmissionJobWindowKickFailedWindows`.

### Hardware checkpoint (after offline GREEN)

Smallest falsifiable run: one KMD-only drill switch (default off) that forces
S2 (a BeginSuccessorJob refusal) once per N kicks; the only variable is the
fallback itself (reverting 56772879 to reproduce EXP2213 would add a second). Expected: `KickFallbacks == drills`, zero ENGINE resets, all fences
reported in order, perf within gate. Recovery: standard rec<id>h/recover.sh.
