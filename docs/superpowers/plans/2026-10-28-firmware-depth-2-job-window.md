# Firmware depth-2 job window (the step after rc90 build-ahead)

Date: 2026-10-28. Status: plan. Parent plans:
`2026-10-09-multi-job-firmware-queue.md` (phases 4-5),
`2026-10-10-kmd-cross-context-render-queue.md` (phase 5a, done).
Step 1 (rc90, `Wom1JobBuildAhead`, default off) is in the tree: early arena
release after the kick, prebind of the successor, chain kick from the worker.

## Why

EXP2021-2023 `Wom1JobTiming971` (`tools/agx_gpu_frame_breakdown.py
--kmd-timing`), median per render job:

| Interval | us | After rc90 step 1 | After depth 2 |
|---|---|---|---|
| complete -> notify (save 9, timestamp 9, release 40) | 56-62 | about 20 | about 20, overlapped |
| notify -> worker (WorkerFinished, DPC, dispatch, wake, bind) | 18-45 | about 5-10 | 0 (N+1 already kicked) |
| BeginJob | 12-46 | same | before N's end |
| build (copy 125-133) | 141-149 | same | before N's end |
| kick -> first progress (firmware) | 331-357 | same | overlapped with N's 3D |

Step 1 removes about 60-90 us per render and 20-40 us per compute job. The
remaining 0.5-0.55 ms per render (BeginJob + build + firmware start) is on
the critical path only because the firmware gets N+1 after N's completion.
Asahi queues it while N runs (`workqueue.rs:64` MAX_JOB_SLOTS 127, a
fragment barrier on the vertex stamp, `queue/render.rs:355-373`).

## What still holds one job (proven from source)

| State | Where | Why it blocks N+1's build/kick while N runs | Change for depth 2 |
|---|---|---|---|
| Per-submission objects 14-19, 16 (InitBM), timestamps 28-35 | `apple_agx_render_shared_memory.c:487` `slotted_object`, `:498` `job_slot` | already two copies by stamp parity | none; depth stays <= 2 (slot reuse at N+2 waits for N) |
| event_count / notifier threshold, object 12 | `:465` `per_submission_object` (12 is CPU-written, not slotted); `build_active_job` threshold `:834` | N+1's build rewrites the value N's notification reads | slot it (third copy pair) or write it at kick time only (m1n1: `event_count += 2` per submitted job) |
| Buffer-manager objects 1, 20, 21, 22 and 13 | `AppleAgxRenderSharedMemoryBuildManagedG4Job` restores `{1,20,21,22,13}` (`:1060`); `AdmissionSaveG4Manager` (`backend_platform_windows.c:2830`) saves after the job; `Owner->ManagerFence` refuses a second build (`:1061`) | the firmware updates the live manager while N runs; restore/save around each job | same-key consecutive jobs skip save/restore (Asahi `buffer.rs:9-28`: one buffer serves several render jobs); a different key drains (depth 1) |
| Compute sidecar, object 39 | `backend_platform_windows.c:3179` ("this job alone uses until its completion") | N+1's prelude/compute would overwrite N's RunCompute | two sidecar halves by stamp parity, like 14-19 |
| Template arena (BackendImage) | step 1: released after the kick in the G4 path | none after step 1 | none |
| RenderPacket (one) | `render_admission.h:813` | holds the running job | two-entry in-flight window (Active, Active2) |
| Scheduler ActiveFence (one) | `apple_agx_scheduler.h:46` | `AppleAgxSchedulerActivateFence` needs ActiveFence 0 | an in-flight list of two, completion in order |
| Backend PendingJob / PendingSubmission | `apple_agx_backend_runtime.h:220-221`; Submit refuses while Submitted (`apple_agx_backend_runtime.c:234`) | one pending job | two-entry FIFO; Ta/D3/ComputeComplete per entry |
| Queue provider PendingFence | `apple_agx_g13_queue_provider.h:94,139` | one staged fence | FIFO of two, completion by stamp per entry |
| Completion transaction, CompletionContext | `backend_platform_windows.c` runtime `Completion` | one transaction | one per in-flight entry, processed in order |
| G3 ActiveProcess / ActiveFence / PrivateCompletionFence | `gpuva_g3_private.h:41,48`; BeginJob requires them clear (`gpuva_g3_windows.c:3685`) | N+1's BeginJob waits for N's CompleteJob | same process: a job count on the lease; another process: drain (phase 6 = VM slot pool) |
| Graph lease (VM slot 1), JobInFlight | `apple_agx_gpuva_g3_graph.h:82,85` | one job per graph | count per graph; one VM slot, so same process only |

## Ordered steps (each a commit with RED/GREEN host tests, then one hardware variable)

1. (done, rc90) build-ahead.
2. Same-key manager skip: when N+1 has N's manager key, skip N's save and N+1's
   restore (depth 1 still). Host test: byte-identical objects 1/20/21/22/13
   versus save+restore when the firmware did not change them. Hardware unknown
   U1 below.
3. Slot object 12 and the compute sidecar (depth 1 still, alternate by
   parity). Host test: the two copies alternate; the threshold N's
   notification reads is not written by N+1's build.
4. Backend runtime, queue provider and completion: two-entry FIFOs, completion
   strictly in order by stamp (the exact done pointer of the oldest entry).
   Host model: two outstanding jobs, completions observed in order; a fault on
   N drops N+1 too (reported as preempted/aborted).
5. KMD window: RenderPacket Active2, scheduler in-flight list of two, G3 lease
   count, BeginJob for the same process while N runs. The worker kicks N+1
   right after N's kick when N+1 is bound (step 1 prebind), the same process,
   the same manager key, no paging fence between them, no preemption.
   Preempt/reset/TDR walk both in-flight entries (the rc63 rule: a faulted
   scheduler reports nothing and starts nothing).
6. (later) VM slot pool: other processes' jobs overlap.

## Hardware unknowns, one variable each

| # | Unknown | One-variable experiment | Pass / fail |
|---|---|---|---|
| U1 | The firmware leaves the manager objects consistent between two same-key jobs (no per-job state but Counter +1) | step 2 alone, depth 1: GravityMark HD 50k with the skip on/off | image identical, partials 0, no Timeout; fail = TVB overflow/garbage -> keep save/restore |
| U2 | A second RunTa/Run3d doorbell with an advanced head is accepted while the first job runs | depth-2 probe of two trivial renders of one process (escape-gated, not apps) | both stamps retire in order within 5 ms; fail = Timeout event or the second job never starts |
| U3 | TA of N+1 starts while 3D of N runs on one VM slot and manager | the same probe, firmware timestamps (objects 28-35, both slots) | TA(N+1) start < 3D(N) end; if never, depth 2 still hides the KMD build only |
| U4 | Start3D queue_cmd_count / event_count accounting with two outstanding | probe with both orders of completion polling; count completion events | one event per job, thresholds monotonic |
| U5 | Which job the firmware drops on a Timeout/Fault with two outstanding, and whether it resumes the other | G4-r3 TDR drill with depth 2 (drill job first, a second job queued) | Wom1GpuReset succeeds, both fences accounted (one aborted, one dropped/resubmitted), DWM survives |
| U6 | When the firmware stops reading a job's slotted objects | depth 2 with the parity rule (N+2 waits for N's completion) and a poison fill of N's slot right after N's completion | no fault; a fault would mean the firmware reads after completion -> keep N's slot until N+1 completes |
| U7 | Two compute jobs in flight with two sidecar halves | depth-2 probe of two compute dispatches | both complete, results correct |

Recovery for every step: the standard GPU-visible baseline and rollback
(recNNNNh/recover.sh, durable preflight). A queue reset reinitialises the
stamps and rings (TaFirstRun) and must clear both in-flight entries.
