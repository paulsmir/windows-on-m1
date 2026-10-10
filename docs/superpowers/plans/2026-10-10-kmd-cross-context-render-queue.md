# Phase 5a: SubmitCommandVirtual stops blocking for other contexts' jobs

Date: 2026-10-10. Status: design. Parent plan:
`2026-10-09-multi-job-firmware-queue.md` (phase 5, "window of two jobs").

## Why

- `AdmissionG4SubmitVirtualEnvelope` waits up to 10 s in
  `AdmissionPlatformRuntimeAwaitWork` (predicate 13) whenever the single
  render slot is busy. dxgkrnl calls `DxgkDdiSubmitCommandVirtual` from its
  scheduler (VidSch) worker thread, so every wait stalls the whole GPU
  scheduler for that adapter: other contexts' packets, paging packets and
  flip processing queue up behind it.
- EXP1133: a second DWM submission blocked there and DWM-only loads lost a
  quarter of their frames (charmap drag 60 -> 44, two-period frames
  1 -> 35 %). With one submission per context (today's UMD) the same wait
  happens whenever a job of another process is running (DWM vs Notepad).
- EXP1131 job timing: Submit -> Worker 40-56 us and Notify -> next Submit
  134 us per job are CPU round trips that a queued job would not pay.

## Contracts

WINDOWS CONTRACT (WDDM 2.x software scheduling, GPU VA):
- VidSch submits a packet with `DxgkDdiSubmitCommandVirtual` and expects the
  call to return promptly; the KMD queues the packet and reports
  `DXGK_INTERRUPT_DMA_COMPLETED` per node in submission-fence order.
- VidSch may submit packets of several contexts before the first completes.
- `DxgkDdiPreemptCommand`: queued, not started packets are dropped; the KMD
  reports `DMA_PREEMPTED` with the last completed fence and dxgkrnl
  resubmits the dropped packets with `Flags.Resubmission`.
- `DxgkDdiResetEngine` / `ResetFromTimeout`: all packets of the engine are
  abandoned.

AGX/ASAHI CONTRACT (unchanged in 5a): the firmware runs one render job at
a time; the worker binds the job image (arena, TA/3D build, header,
command), BeginJob leases the process VM slot, the manager is restored and
saved around the job, completion is detected by event + stamp.

TRANSLATION:
- A small adapter FIFO of pending render submissions (K = 2 pending besides
  the active RenderPacket). Each entry is a snapshot of everything the bind
  needs: packet description, the `APPLE_AGX_G4_SUBMIT_VIEW` scalars, copies
  of the native command (<= `APPLE_AGX_G4_NATIVE_MAX_BYTES`), render struct
  and the one colour attachment, mapping generation, DMA VA/size, context
  and private scene.
- Envelope: validation, parse and private-scene bookkeeping stay as today.
  If the RenderPacket is Empty and no entry is pending, the existing
  prepare/bind/queue path runs unchanged. Otherwise, if the submitting
  context has nothing outstanding and an entry is free, the snapshot is
  stored, the context's `FenceOutstanding` is set (per-context depth stays
  1), the fence is queued in the scheduler FenceQueue, and the call returns
  STATUS_SUCCESS without waiting. A context that already has a job
  outstanding keeps today's bounded wait (phase 5b removes it).
- Dispatch: a head scheduler fence that matches the head pending entry
  while the RenderPacket is Empty kicks the worker like a Queued packet.
- Worker (PASSIVE): before activation, if the RenderPacket is Empty and the
  head pending entry's fence is the scheduler's queued fence, prepare the
  packet from the snapshot, rebuild the view from the copies, run
  `AdmissionBackendImageBindG4Submission`, set the context's mapping
  generation / DMA VA, and queue the packet; then activate as today.
  BeginJob already re-validates DMA range, mapping generation and output.
- PrivateCompletionFence no longer rejects a queued (deferred-bind)
  submission at submit time; BeginJob still requires it to be clear, and
  the worker clears it (PrivateReported) before it finishes.
- Preempt / reset / stop walk the pending entries: each is dropped exactly
  like a Queued packet (context FenceOutstanding cleared, private scene
  unqueued, fence handled by the scheduler's preempted queue or reset).

WHAT IS STILL UNKNOWN:
- Whether dxgkrnl's VidSch then submits cross-context packets ahead in
  practice (expected: yes, when the DDI returns at once) and how much frame
  pacing improves with the UMD still one-in-flight per context. This is a
  hardware measurement, not a design question.

## Ownership

| Concern | Owner |
|---|---|
| Pending FIFO | KMD adapter, under SchedulerLock |
| Snapshot copy | Envelope (submitter thread, PASSIVE) |
| Bind | Worker (PASSIVE), only into an Empty RenderPacket |
| Per-context depth | `FenceOutstanding` (still one job per context) |
| Ordering | Scheduler FenceQueue (unchanged) |

## Tests (host, deterministic)

- Replay (`tests/g4_submit_virtual_replay.c`): with the RenderPacket busy,
  a second context's valid submission returns success, is pending, and its
  fence is queued; the same context's second submission is not queued.
- The worker bind of a pending entry produces the same BackendImage state
  as the direct path for the same submission.
- Preempt and reset drop pending entries and clear their contexts.
- Backpressure test updated: AwaitWork is only used for same-context
  submissions.

## Hardware checkpoint

One variable (5a only, UMD unchanged). Expected: Notepad drag and typing
and DWM-only loads >= EXP1132; fewer two-period frames; job timing
Submit -> Worker shorter for cross-context jobs. Failure: TDR, rejects,
private-scene failures, worse pacing. Recovery: standard recNNNNh.
