# Multi-job firmware queue: more than one render job in flight

Date: 2026-10-09. Status: phase 1 in progress.

## Why

- EXP1113: one job costs ~650 us Submit -> Notify. About 415 us of that is
  GPU and firmware work (kick -> TA end ~175 us, 3D ~140 us, finalize until
  the CPU sees it ~100 us). The rest is KMD work on the critical path.
- DWM issues ~6 jobs per frame and Notepad ~10. All of them run strictly one
  after another.
- EXP1116: with memory no longer the limit (EXP1115), keeping two
  submissions queued in the UMD only moved DWM ahead of applications. The
  KMD still runs one packet at a time: Notepad dropped 34 -> 24 flips/s.
- The single-slot design must go. A real driver queues several jobs to the
  firmware. The TA of job N+1 then overlaps the 3D of job N, and other
  processes' jobs interleave without a CPU round trip.

## Sources inspected

- **Asahi Linux** (`drivers/gpu/drm/asahi`):
  - `workqueue.rs`: rings of command pointers, up to 127 job slots, `RunWorkQueue` with the new wptr.
  - `event.rs`: 128 event slots. A stamp is initialised once to `slot << 24`, the firmware stamp is zeroed, and the CPU never rewrites either.
  - `queue/mod.rs`: separate vertex and fragment work queues per context. "Frag always serializes".
  - `queue/render.rs`: `stamp_value = ev.value.next()` in steps of 0x100; a fragment barrier waits on the vertex stamp.
  - `buffer.rs`: a buffer manager is shared by concurrently active scenes, with one manager slot per buffer.
- **m1n1** (`proxyclient/m1n1/agx/render.py`, `fw/agx/cmdqueue.py`):
  - TA/3D stamps 1 and 2 are initialised once, values `+= 0x100` per job.
  - `EventControl.event_count` is CPU-owned, `+= 2` per submitted job.
  - `Start3D.queue_cmd_count = prev_stamp_3d >> 8`.
- **This repository**, nine single-slot layers (file:line map in the session report, summarised here):
  - render packet;
  - per-context fence;
  - scheduler active fence;
  - platform worker that polls each job to completion;
  - one backend image/arena;
  - per-submission objects copied to fixed addresses: work commands 14-19, stamps 9/10/26/27, `event_count` 12;
  - CPU-rewritten stamps;
  - buffer-manager save/restore around one firmware manager slot;
  - one VM slot (1);
  - single pending state in the backend runtime, G13 queue provider and queue runtime;
  - completion matching that requires an exact done pointer.

## Contracts

- **Windows (WDDM 2.x software scheduling):** dxgkrnl's scheduler may call `SubmitCommandVirtual` ahead of completion. The KMD reports `DMA_COMPLETED` per fence in submission order per node. Nothing requires one packet at a time; the limit is ours. `SubmitCommand` currently blocks up to 10 s for the previous job (`AdmissionPlatformRuntimeAwaitWork`).
- **AGX/Asahi:**
  - Work queues hold many commands.
  - Completion is a monotonic stamp per event slot.
  - Every job needs its own command objects (TA/3D work commands, barrier, micro-sequences, InitBM) and its own scene.
  - A buffer manager can serve several active scenes.
  - Different VMs need different VM slots.
- **Translation:**
  - per-job command objects in a small ring of K slots;
  - firmware-owned stamps;
  - completion per job by stamp;
  - KMD packet/fence window of depth K;
  - per-process buffer-manager state that the firmware keeps live, without CPU save/restore;
  - VM slots per process.

## Ownership

| Concern | Owner |
|---|---|
| Stamps | Firmware (CPU initialises at queue creation/reset only) |
| `event_count` | KMD, monotonic per submission |
| Command objects | KMD per job slot |
| Rings | Shared, KMD writes wptr, firmware advances done |
| Manager state | Firmware per manager slot; KMD only on (re)bind |
| VM slot | KMD per process while it has jobs |
| Recovery | Queue reset reinitialises stamps and rings (TaFirstRun) |

## Phases (one hardware variable each)

Each phase is preceded by host tests (RED -> GREEN where it is deterministic) and recorded in EXPERIMENTS.md and CHANGES.csv.

1. **Firmware-owned stamps.** Objects 9/10/26/27 are written only when the queues are (re)initialised, never per job. `event_count` stays per-submission.
   - Checkpoint: identical behaviour and flips; the stamps keep advancing by 0x100 per job across jobs.
   - Failure: a job never completes (timeout, TDR) -> rollback.
2. **Completion by stamp per job.** Drop the exact done-pointer requirement; a job is done when its event fired and its stamp is at or past its value. Checkpoint: identical behaviour.
3. **K = 2 command-object slots.** Per-submission objects 13-19 and the timestamps 28-35 alternate between two shared-memory copies. Still one job in flight. Checkpoint: identical behaviour; the slots alternate.
4. **Persistent per-process manager.** BM Info/BlockControl/Counter/Stats live in the process's private manager storage. Each manager gets its own firmware buffer-manager slot. InitBM only on slot (re)assignment or growth; no save/restore copies. Checkpoint: identical rendering; InitBM count drops to rebinds.
5. **Window of two jobs, same VM.**
   - Backend runtime, provider, queue runtime and platform worker hold up to two pending jobs.
   - The KMD packet/fence/scheduler window becomes 2, and submit stops blocking.
   - Only jobs of the same process may overlap at this phase.
   - Checkpoint: TA of N+1 starts before 3D of N ends (firmware timestamps); DWM's frame time drops.
6. **VM slot pool.** Jobs of different processes may overlap. Checkpoint: Notepad and DWM interleave; flips improve for both.
7. **UMD two submissions in flight** (re-apply e4398e22 on top of 5/6).

## Recovery

Every phase uses the standard GPU-visible baseline and rollback (`recNNNNh/recover.sh`, durable preflight). A queue reset must reinitialise stamps and rings (phase 1 relies on TaFirstRun).

## Site map for phase 5 (2026-10-10, read-only survey of the G3 build)

Short names: g3 = render-admission/src/gpuva_g3_windows.c, bp =
backend_platform_windows.c, sch = scheduler_windows.c, wq =
work_queue_windows.c, rbi = render_backend_image.c (line numbers as of
f1617c42; re-check before editing).

- Entry: SubmitCommandVirtual (g3 ~3375) -> Inner -> AdmissionG4SubmitVirtualEnvelope
  (g3 ~3025). Predicate 13 is AdmissionPlatformRuntimeAwaitWork (blocks up to
  10 s for the slot). Under state->Lock it rejects while PrivateCompletionFence
  is set, looks up the private scene (FindPrivateScene refuses while the
  context's GpuvaG3PrivateFence is nonzero), parses, sets the scene
  Queued/Fence and the context Private/Preempt/CancelFence. Under
  SchedulerLock it prepares the single RenderPacket only if it is Empty and
  the context's FenceOutstanding is 0, then binds (rbi
  AdmissionBackendImageBindG4Submission: arena, TA/3D build, header, command
  copy <= 4096 B, lease, BoundFence) and queues the fence.
- Single-job state: RenderPacket (Empty/Prepared/Queued/Active),
  BackendImage (BoundFence, G4Header/Command/Lease, G4Manager), scheduler
  ActiveFence/DispatchedFence (cleared only by the render DPC, sch ~289),
  state ActiveProcess/ActiveFence, PrivateCompletionFence (set at CompleteJob,
  cleared at PrivateReported in the DPC path), graph JobInFlight/LeaseToken
  (slot 1), backend/provider/queue-runtime pending job, context
  FenceOutstanding and Private/Preempt/CancelFence.
- Worker (bp): kick at head fence when Active/Dispatched/PagingPending are 0;
  BeginJob (g3 ~2372) requires ActiveProcess NULL and no
  PrivateCompletionFence, re-validates DMA range and mapping generation;
  PrepareG4Manager; backend submit; poll; AdmissionBackendComplete
  (SaveG4Manager, CompleteJob, CompleteActiveFence, image restore, packet
  Empty, DMA_COMPLETED); DPC clears DispatchedFence and dispatches;
  WorkerFinished signals SlotEvent.
- Depth 2 with the firmware still serial needs per entry: packet
  description, a copy of header + command + render + attachment + lease +
  HeapBlocks, DmaBufferVa/Bytes/MappingGeneration, completion context;
  FenceOutstanding and the context Private*/Preempt/CancelFence become per
  entry (scene->Fence already is); PrivateCompletionFence stops gating
  submit; the bind moves into the worker before BeginJob. Arena, G4Manager,
  graph lease, ActiveProcess and backend pending state can stay single.
- Must also walk all entries: PreemptCommand (sch ~330), ResetEngine
  (sch ~432), BackendRetire/PlatformRuntimeReset (bp), CancelCommand,
  SchedulerStop. Tests to update: test_g4_envelope_backpressure,
  g4_submit_virtual_replay (occupied packet rejects), render worker
  lifetime, render work queue.

## Firmware-side map for overlap (2026-10-10, read-only survey at dc2d0b35)

BR = shared/src/apple_agx_backend_runtime.c, QP/QR = g13 queue provider /
runtime, RSM = render_shared_memory.c, BP = backend_platform_windows.c.

Blockers that exist even before any KMD window:
- BR:407-410 requires the exact done pointer of the single pending job;
  once N+1's ring entries retire this fails (phase 2 fixed only QR).
- RSM:807-808 / BP:2229-2242 refuse to build or prepare while a job's
  manager fence is set; objects 1/20/21/22 (BM Info, BlockControl,
  Counter, Misc) are not slotted and are restored from saved state per job,
  which would overwrite firmware-live BM state while N runs.

Single-job state to generalise (in order): observation batches carry no
fence (BRH:98-104, QPH:6/34-41, PC:225-236); QR pending records and
completion slot (QRH:133-148, QR:313-315/383-404); QP staged + pending
fence (QPH:132-135, QP:174-176/320-358/610-692); BR PendingJob/Phase
(BRH:184-197, BR:222-268/288-318/357-391/448-474); manager save only on the
newest fence with a same-key live manager (RSM:766-847); per-entry
QueueObjects timing copies (BP:139-140, 344-423); release the BackendImage
right after Submit instead of at completion (hypothesis: the arena is CPU
staging only in the G4 path); worker submits N+1 from its poll loop (same
process, same manager key, different scene, no compute); depth <= 2 because
objects 13-19 and 28-35 alternate by stamp parity.

Ordered minimal list: (1) RED tests per step; (2) drop BR exact done
pointer; (3) fence-tagged observations, 4 per batch; (4) QR 2-deep;
(5) QP staged slot + FIFO, manager change needs a drain; (6) BR 2-entry
FIFO; (7) live same-key manager, save on newest fence; (8) per-entry timing
objects, early image release; (9) worker submits N+1 in its poll loop;
(10) per-entry completion; (11) Windows packet window (site map above).

Only hardware can answer: whether TA(N+1) starts while 3D(N) runs on the
same manager and VM slot; whether two scenes can be live on one template
manager without per-job writes beyond Counter +1; TVB heap exhaustion with
two scenes (GrowTvb ignored, QR:504-505); whether a second doorbell with an
advanced head is accepted while busy; Start3D queue_cmd_count/event_count
with overlap; which job Recover drops with two outstanding; when the
firmware stops reading a job's objects (slot reuse at N+2); any per-job
maintenance between jobs of one context.
