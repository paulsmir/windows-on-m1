# P1: why DWM's composition job waits 0.67-2.94 ms after GravityMark's frame

Offline pass over current source and the existing EXP2171 receipts (no hardware). Class:
**FULL GRAPHICS** (WDDM 3.x KMD, GPUVA, software VidSch, DMA-buffer-boundary preemption).

## Evidence used

- Source (worktree `integration-ad04-windows-compiler`), `drivers/apple-agx/render-admission/`:
  `src/gpuva_g3_windows.c` (submit dispatch 6347-6430, present compute 5703-5856),
  `src/present_windows.c` (Blt encode / present submit 61-426), `src/work_queue_windows.c`
  (CPU queue + dispatch 3-223), `src/paging_windows.c` (CPU worker/notify/DPC 268-318,
  426-533, 637-672, 764-771), `src/callbacks.c` (DxgkDdiPresent 186-300),
  `src/scheduler_windows.c` (PreemptCommand 418-540, TryNotify 72-160),
  `src/backend_platform_windows.c` (timing 412-445, chain 5379-5399, activation 5946-6012,
  AwaitWork 8252-8310), `src/render_job_ahead.c:55-68`, `src/lifecycle.c:778-800`,
  `shared/include/apple_agx_scheduler.h:6-60`, `shared/src/apple_agx_scheduler.c:162-238`.
- WDK 26100 `d3dkmddi.h`: `DXGK_PRESENTFLAGS` 167-199, `SchedulingCaps.HwQueuePacketCap`
  2016 ("maximum number of DMA packets allowed to be queued to a node"),
  `DXGK_SUBMITCOMMANDFLAGS.Resubmission` 4320.
- EXP2171 receipts `$HOME/public_windows/.local/experiments/EXP2171-m1n1split2/evidence/EXP2171/`:
  `kmd-receipts-mid-a.txt` (Wom1JobTiming971 v4, Wom1G2PresentPath, Wom1DdiStatus,
  Wom1G4PreemptOverlap, Wom1SubmitPath1135), `rcpt-final/Wom1G3PagingProfile.bin`,
  `gm2171a-umd.log`. Ring decoded with a throw-away script (slot = 288 B, header 32 B).

## Answer 1: the fence between GravityMark and DWM is NOT a present packet

The premise "fence N = present-flagged DMA via AdmissionPresentSubmitVirtual" is contradicted:

1. A `Flags.Present` virtual submission returns at `gpuva_g3_windows.c:6373-6374` **before**
   `AdmissionJobTimingStartWindows` (6375-6378). It can never own a ring slot. Fence 37215
   (also 37225, 37261) **has** a slot: Submit stamp, pid 1516, GravityMark's own context token
   (`...824d50`, same as 37214/37217), DmaBufferSize 448 (= GravityMark's render buffers, e.g.
   37217). Every later phase (Worker, BeginJob flag, Complete, Notify) is zero, and
   Complete is stamped unconditionally on every backend completion
   (`backend_platform_windows.c:4346`, the JobDiagnostics gate exempts it at 435).
2. Not a touch: a touch submits `CommandLength = 4` (`umd_gpuva_windows.c:1146`), not 448, and
   GravityMark's last `measure-touch` is at QPC 3541.6 M while 37214 completes at 4666.9 M.
3. Not a rejected submit: `Wom1DdiStatus` slots 42 (DxgkDdiPresent) and 63
   (DxgkDdiSubmitCommandVirtual) have zero failures.
4. `Wom1G2PresentPath`: only 3 present-compute attempts in the whole boot (all reason 6, CPU
   path), and the C67 ETW result already showed dxgkrnl sends no app Blt to this KMD.
   GravityMark's flip-model present is a composition token; its "present" does not occupy
   node 0.
5. What it is (strongest explanation): **GravityMark's next render, discarded by a boundary
   preemption.** 37214's chain refusal is 4 (`AheadRefusals` = 0x40000;
   `render_job_ahead.c:60`: "A boundary preemption waits for this completion"), i.e. a
   DxgkDdiPreemptCommand was pending when 37214 finished. PreemptCommand discards the queued
   render packet / pending entries (`scheduler_windows.c:507-528`,
   `AdmissionG4PendingDropLocked`) so they never get Worker/Complete. DWM's job is submitted
   22 us after 37214's Notify, and GravityMark's next 448-B render 37217 at +1031 us, after
   DWM, i.e. VidSch's resubmission order. Wom1G4PreemptOverlap: 2689 preemptions /
   36752 submissions / 330 resubmissions (stale, published every 64th preemption).

For completeness, what a present-flagged packet is in this driver (not on this frame's path):
- GPU path: `AdmissionG3SubmitPresentComputeInner` (5703-5856) queues a compute blt
  (`ADMISSION_JOB_WINDOW_KIND_PRESENT`) into the **same single RenderPacket slot** and fence
  queue; it completes through the backend DMA_COMPLETED (`backend_platform_windows.c:4244`).
- CPU path: `AdmissionPagingSubmitPresent` -> `AdmissionCpuQueueSubmit(PRESENT)` -> work item on
  CriticalWorkQueue (`paging_windows.c:531-533`) -> CPU copy
  (`AdmissionMemoryRuntimeExecutePresent`) -> `DxgkCbSynchronizeExecution` ->
  `DXGK_INTERRUPT_DMA_COMPLETED` + DPC (`paging_windows.c:268-318`).
- Neither waits for vblank, a scanout-broker latch, or a timer. Flips (`Flags.Flip`,
  FlipOnVSyncMmIo) carry no DMA (`callbacks.c:222-300`); the flip is
  `SetVidPnSourceAddress` -> `AdmissionScanoutQueuePresent` (`display.c:1243`) and is
  reported by CRTC_VSYNC on latch (`scanout_windows.c:1452-1458`).

## Answer 2: why the other process's job cannot start

- One render node: `NbAsymetricProcessingNodes = 1` (`lifecycle.c:780`; node 1 exists only
  for live paging), `APPLE_AGX_SCHEDULER_NODE_COUNT 1` / `ENGINE_COUNT 1`, one fence FIFO
  (`apple_agx_scheduler.h:6-40`). `AdmissionDispatchQueuedWork` activates only
  `QueuedFence` when `ActiveFence == 0` and dispatch is not blocked
  (`work_queue_windows.c:141-145`). One RenderPacket slot (`render_admission.h:864`). No HWS.
  So any node-0 packet, of any process, serializes all others; this is normal for software
  VidSch.
- Granularity is DMA-buffer boundary (`lifecycle.c:790-793`): a preemption must wait for the
  running packet (37214) and then report DMA_PREEMPTED (`scheduler_windows.c:43-70`).
- In the sample the bubble is **after** DWM's submit: DWM 37216 submitted at +943 us,
  GPU idle from +921 (37214 Notify) until DWM's Worker at +3879 (2.96 ms). Same shape for
  37226 (+0.67 ms) and 37262 (+3.04 ms). DWM jobs **not** preceded by a discarded GravityMark
  fence (37244, 37251) are picked up in 49 / 123 us. So dxgkrnl is not holding DWM; the KMD
  holds it after a preemption.

## Answer 3: completing the present immediately or moving it to another node

- Not applicable to the measured bubble: no present packet is on this boundary.
- For reference: a present/Blt DMA is complete when its copy is done; flip retirement is
  separate (CRTC_VSYNC / present history). The CPU-path present already completes as soon as
  the copy is done; the GPU path completes at job end. Moving presents to another node would
  need a second node/engine that dxgkrnl targets for the presenting context; it does not touch
  this bubble.

## WINDOWS CONTRACT

- VidSch preempts lower-priority queued work for the DWM context; the KMD reports
  DMA_PREEMPTED at the boundary and VidSch resubmits preempted packets (`Resubmission`,
  d3dkmddi.h:4320). `HwQueuePacketCap` bounds how many packets VidSch queues to the node
  (d3dkmddi.h:2016). Set to 2 only with general depth-2 (`lifecycle.c:788-789`), otherwise 0
  (VidSch default; exact default is undocumented).
- DXGK_INTERRUPT_DMA_COMPLETED must arrive in fence order per node.

## AGX/ASAHI CONTRACT

Firmware runs one job at a time per queue; there is no host cancellation of a kicked job
(scheduler_windows.c:575-585, ASAHI-QUEUE-MODEL-2026-11-02.md). Discarding a queued,
unkicked job is host-only bookkeeping (backend image release, private scene preempt); no
firmware action is needed. Not re-read in this pass.

## TRANSLATION

Preemption of an unkicked queued packet = drop + DMA_PREEMPTED at the running packet's
completion; the next dispatch should follow the commit with the normal pickup latency
(<=150 us observed). The observed 0.67-3 ms means one of the post-preemption steps in the KMD
is slow or waits for a later wakeup.

## WHAT IS STILL UNKNOWN (ranked by causal proximity)

- H1 (strongest): DMA_PREEMPTED delivery / commit is deferred. `TryNotify` returns "accepted and
  deferred" while the render worker that ran 37214 is still scheduled or a CPU packet is in
  flight (`scheduler_windows.c:96-103`), and dispatch stays blocked until Commit
  (`AppleAgxSchedulerDispatchBlocked`). The retry comes only from WorkerFinished or a later
  event.
- H2: DWM's SubmitCommandVirtual blocks in `AdmissionPlatformRuntimeAwaitWork`
  (reason 16, "render slot still owned", 10 ms backstop, `backend_platform_windows.c:8262-8292`)
  until the discarded 37215 releases its backend image or slot. Submit is stamped before
  the envelope (6376), so this wait is counted in Submit->Worker. Wom1SubmitPath1135 shows
  only 88 slot waits (mean ~0.95 ms, max 9.4 ms), which argues against H2 for most frames,
  but that receipt is stale.
- H3: DWM was queued as deferred (Phase 5a pending) and the bind wakeup for the pending head is
  missed until GravityMark's resubmission or the job thread's next wake.
- Not distinguishable offline: the receipts have no per-fence PreemptCommand, DMA_PREEMPTED,
  Commit or AwaitWork stamps.

## Ownership

KMD scheduler/preemption (`scheduler_windows.c`) and platform worker
(`backend_platform_windows.c`) own the defect. m1n1, Mu and the UMD are not involved: no
firmware state is touched, and the UMD submit pattern is valid WDDM.

## Ranked fix options (after the discriminator)

| # | Change | Expected saving | Risk |
|---|---|---|---|
| 1 | H1: report DMA_PREEMPTED immediately when only unkicked work was dropped and the boundary fence has retired; on Commit, dispatch synchronously (no WorkerFinished dependency) | ~0.6-2.9 ms on ~60% of frames (sample: 3 of 5 DWM jobs), about 1-1.5 ms/frame mean, up to ~5% FPS (39.1 -> ~41) if the GPU is the limiter | Preemption ordering bugs show as 0x119/TDR. Existing preemption host tests cover the scheduler model |
| 2 | H2: release the discarded job's slot/backend image inside PreemptCommand (already partly done: `AdmissionBackendImageReleaseSubmission`) and set `SlotEvent` so AwaitWork wakes at once | same window, if H2 | Freed image reused while the worker still references it |
| 3 | Avoid the preemption: `HwQueuePacketCap = 1` on node 0 so GravityMark's next packet stays in VidSch and DWM is simply submitted next | removes the bubble, but adds VidSch submit latency to every GravityMark job (~9/frame x 20-50 us = 0.2-0.5 ms) and disables prebind/chain/depth-2 | Throughput regression. Only if 1/2 fail |
| 4 | Present fast-completion / separate present node | ~0 for this boundary | Not justified by evidence |

## Smallest falsifiable checkpoint (receipt-only, one variable)

WHY THIS HYPOTHESIS: (1) chain refusal 4 at 37214 (preemption pending at its completion);
(2) every slow DWM pickup follows exactly one discarded GravityMark fence, while the two DWM
jobs without one are picked up in 49/123 us; (3) the discarded fence has a Submit stamp and
no other phase, which only a dropped, unkicked submission produces.

- Variable: a receipt-only Wom1JobTiming extension (no behaviour change). Per preemption, record
  QPC at PreemptCommand entry (+PreemptionFenceId, cutoff, active, queued fence), at
  TryNotify deferral (and which predicate), at DMA_PREEMPTED notify, and at Commit. Per
  envelope submit, record AwaitWork entry/exit and its reason, and whether it went
  immediate or deferred. Use free Sub[] slots or a 64-entry preemption ring.
- Run: the EXP2171 config (GravityMark D3D12 100k -temporal 0, HD PERF, same m1n1/profile),
  mid-arm and final receipts.
- Verdicts: H1 if Notify->Commit or deferral spans most of the bubble; H2 if DWM's AwaitWork
  spans it; H3 if Commit precedes DWM's submit and AwaitWork is ~0 while Worker lags.
- Pass/fail: suite 119/0, no TDR/0x119; HD PERF unchanged within noise (receipt-only).
- Recovery: rec2171h-style visible recovery (same m1n1 075e3980 profile). Ledger
  before/after in EXPERIMENTS.md.
- Then implement the matching fix (1 or 2) with a host scheduler test of the deterministic
  part (drop-unkicked -> immediate notify -> dispatch), and run one HD PERF A/B.
  Expected: DWM Submit->Worker <= 150 us on preempted frames, FPS +2-5%.
