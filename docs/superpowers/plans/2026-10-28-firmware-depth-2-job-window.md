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

## rc95 findings (offline, source only) and the revised order

Sources: Asahi `workqueue.rs`, `queue/mod.rs`, `queue/render.rs`,
`gpu.rs`, `event.rs`, `buffer.rs` (`.local/reference/asahi-linux-asahi`,
G13 V13.5, the J313 firmware); this tree at rc94.

- **Order without the CPU.** N+1's first queue leads with a firmware
  Barrier on N's last stamp (Asahi explicit `vdm_barrier`/`cdm_barrier`,
  `queue/mod.rs:787-820`). Windows' in-order node semantics stay exactly
  depth 1; only the KMD work and the kick latency leave the critical path.
  The TA Barrier on a compute stamp is hardware-validated (D3D-G1b).
- **U2 (second doorbell while busy): answered by source.**
  `JobSubmission::run` publishes the new wptr and sends RunWorkQueue with
  `is_new = false` whatever the queue still holds (`workqueue.rs:500-531`,
  `gpu.rs:1101-1126`); `can_submit` checks only free slots and ring space
  (`workqueue.rs:391-394`).
- **U4 (completion accounting): the notifier threshold defers N's event.**
  Asahi raises the threshold by the job's notification count when the job
  runs (`queue/mod.rs:284-330`, `notification_count = 2*nr_render +
  nr_compute`, `:741`); the firmware raises the event only at the
  threshold (EXP1278 deadlock). Our builds write the threshold absolutely
  (`apple_agx_render_shared_memory.c:918-934` render, `:742-750` compute), so
  N+1's build moves it past N: N's event arrives when N+1 ends. Our
  completion requires the event (`apple_agx_g13_queue_runtime.c:554`,
  `apple_agx_g13_codec.c:175`). Asahi's `signal()` completes every command
  whose driver stamp reached its value (`workqueue.rs:852-940`). Unknown:
  whether the driver stamp itself waits for the threshold (EXP1213 saw a
  compute done pointer advance with stamp 0, off the notifier). The rc95
  receipt mode measures it on the default depth-1 path: a render's TA
  (1 of its 2 notifications) and a prelude (1 of 3) end before their
  threshold is reached.
- **U5 (fault with two outstanding).** Asahi marks every pending stamp
  record (`gpu.rs:1004-1040`, culprit slot -> error, others Killed) and
  resumes (`gpu.rs:1059-1095`); later queue entries keep running. Our model
  covers both outcomes (N+1 runs after the drop, or is killed with it).
- **U6 (slot reuse).** Asahi frees a command's objects when `signal()`
  sees its stamp (`workqueue.rs:910-916`). With depth 2 and parity slots,
  N+2 is built only after N is reported.
- **U7 (compute sidecar).** G4 compute jobs and v5 preludes use the
  envelope's preemption buffer (`backend_platform_windows.c:2919`,
  `:3184`); a job owns only the sidecar's work (0x8000), microsequence
  (0x8400), TA barrier (0x8800) and timestamp (0x9000-0x9420) words.
  A second set fits in the free 0x9420-0xc000 range of object 39; the
  statistics, firmware stamp and gpu_buf stay shared (work-queue owned).
- **U1 is rarely needed.** Each private-scene render's manager key carries
  its scene storage (`gpuva_g3_windows.c:3705-3716`), and the GravityMark
  frame alternates compute and render (CRCRCRCR after rc94). Render ->
  render pairs need the same scene; compute <-> render pairs share no
  manager objects.
- **VidSch gating.** sub(N) ~ cmp(N-2) + 50 us (rc94): when N is kicked,
  N+1 is (about) submitted and N+2 is not. Depth 2 is the deepest window
  VidSch fills; a depth-2 window always has N+1 to kick.

Revised order (each one hardware variable):
1. rc95: decisions + window model (`render_job_window.c`) and receipt mode
   (`Wom1JobDepth2`, timing v4): pair classes, refusals, successor lead,
   TA/prelude stamp lags. Decides U4 and which capability unlocks the most.
2. Second sidecar job-word set (U7) and the compute-queue Barrier.
3. Queue runtime/provider/backend: two outstanding entries, completion of
   the older one from its own stamps (if U4 allows) or batched with N+1.
4. KMD window: G3 lease count for one process, second BeginJob, chain of a
   kicked successor, preempt/reset over both entries (render_job_window.c).

## rc98 (step 2): second sidecar set and the compute-queue Barrier

- **Set B** (`apple_agx_render_shared_memory.h`): the job's RunCompute
  work, microsequences, TA Barrier, new compute-queue Barrier slot
  (0x8820) and timestamp block, 0x2000 above set A (0xa000-0xb420, inside
  the free 0x9420-0xc000). Statistics, firmware stamp, gpu_buf and the
  sidecar's own preemption buffer stay shared, so set B needs a job-owned
  preemption buffer. Set A is byte-identical to rc97 (golden digest).
- **Choice**: `AppleAgxRenderSharedMemoryNextJobWordSet` alternates per new
  stamp (a rebuild keeps its set; a new compute queue starts with A), so
  consecutive sidecar jobs never share words. A successor's depth-2
  leading Barrier is built in the set that call names for its stamp.
- **Barriers**: `AppleAgxG13BarrierEncode` (the D3D-G1b layout, which
  reproduces the captured 3D-on-TA Barrier) for TA on a compute or 3D stamp
  and for the compute queue on a 3D or compute stamp (Asahi uses one
  raw::Barrier for vdm and cdm barriers). Wait stamps: TA object 9, 3D
  object 10, compute sidecar 0x8780.
- **Wiring**: `Wom1JobDepth2Caps = 1` (with Wom1JobDepth2 and build-ahead)
  declares SECOND_SIDECAR | TA_ON_3D | COMPUTE_BARRIER in the receipt-mode
  decision and makes compute jobs and v5 preludes alternate sets at
  depth 1. LIVE_MANAGER is not built. Nothing is kicked early; no path
  publishes a leading Barrier yet (step 3/4).

## rc102 (steps 3-4): the wired window, Wom1JobDepth2Kick = 1

On top of Wom1JobDepth2Caps; default off. EXP2034 decided U4: driver stamps
are held until the notifier threshold, so a completion is reported by
event, possibly two jobs at once.

- **Shared layers.** Queue runtime successor slot (observed with every
  event, promoted by TakeCompletion, complete at once when batched; a
  recovery retires both jobs and judges each by its own unshared stamps);
  provider RunSuccessor/PlanSuccessor and one batch for a pair; backend
  SubmitSuccessor and promotion after the running job's Io.Complete;
  scheduler KickedFence (kept through a boundary preemption, the newest
  aborted fence at an engine reset); G3 graph nested job.
- **KMD.** The poll loop of job N kicks its Prebound successor when the
  window allows (rc95 refusals + 61 idle queues + 62 job-word sets): the
  entry turns Kicked under PagingLock+SchedulerLock, G3 BeginSuccessorJob
  joins N's lease, the build leads the successor's first queue with a
  Barrier on N's last firmware stamp (3D object 10 behind a render, sidecar
  0x8780 behind compute) in the successor's own job-word set, and
  RunSuccessor publishes it. The successor is adopted (activated) inside its
  own completion when one event ended both, else by the worker after N's
  report; the worker then polls it without a kick.
- **Reset.** The quiescence waits for the newest kicked job; both ends are
  reported through the ordinary completion; a kicked fence the firmware
  never received is aborted by the engine reset (LastAborted = newest),
  never resubmitted; never-kicked entries go back to VidSch.
- **Receipts.** Timing v4 flags DEPTH2_KICKED / DEPTH2_BATCHED / Barrier
  class; Wom1JobWindow (kicks, max in flight, batch sizes, Barriers,
  adoptions, kick failures).
- **rc100.** The poll planner learns no duration from a job whose end was
  deferred to its successor's or an adopted successor (its poll began after
  its start); both still count in the Wom1JobPoll statistics.
