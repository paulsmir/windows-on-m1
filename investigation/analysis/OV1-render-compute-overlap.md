# OV1: render/compute overlap Apple uses and we do not (2026-11-05)

## Measured (EXP2192, macOS 15.7.8 Metal on this J313, per-encoder command buffers, FHD 100k -temporal 0)
- 21 encoders per frame; median start -> end (ms from frame start):
  - c15 (lighting, ~our C0) 13.09 -> 16.61 runs fully inside r16 (main pass, ~our R1) 13.08 -> 20.14 in every frame;
  - c18/c19 overlap r20 (0.45 + 0.91 ms); r20 of frame N overlaps c0 of frame N+1.
  - Everything else (the C4 block c0-c9, r10 shadow, c11/c13, r14 G-buffer) is serial.
- Summed encoder time 26.7 ms vs frame span 22.4 ms (FHD); 19.7 vs 17.4 ms (HD): overlap saves Apple 4.3 / 2.3 ms.

## Our side
- The UMD counts D3D12 ResourceBarrier calls only (agx_d3d12_ddi.cpp ResourceBarrier, rc105); ordering comes from
  Asahi's BO hazard tracking and rc105 submit order; the KMD serializes every render <-> compute job boundary with a
  firmware Barrier (depth-two pairs, Q2c cross-queue barriers on global stamps).
- EXP2066 (rc118 measure-batch-deps) found "every C -> R depends" -- at Mesa BO granularity (4096-bit Bloom of BO
  handles). GravityMark's D3D12 resources are placed in heaps (one BO per heap), so BO conflicts are an upper bound
  that cannot separate independent passes; its "<= 2 ms" ceiling is superseded by Apple's measured 4.3 ms.

## WINDOWS CONTRACT (to verify against the D3D12 spec)
- Work recorded between ResourceBarrier calls may execute concurrently; the application must place a barrier (UAV,
  transition, aliasing) for every RAW/WAR/WAW hazard on a resource (render target / depth states included).
- Therefore two consecutive jobs with no barrier between them referencing a resource the first writes are allowed to
  overlap.

## WHAT IS STILL UNKNOWN
- The exact barriers GravityMark records between C0 and R1 (and C18/19 vs R20) and their resources.
- How macOS decides independence (RE of com.apple.AGXG13G / IOGPUFamily in progress) and how the firmware runs a
  render and a compute job concurrently (separate queues -- available since Q2c -- without the cross-queue Barrier).

## Next (smallest discriminator first)
1. Receipt only: per job boundary, the D3D12 barriers recorded between the previous job's last command and this job's
   first command, with resource identity vs the previous job's written resources.
2. If C0 -> R1 has no hazard barrier: KMD kicks R1 on Queue A without the Barrier on C0's stamp (one switch), measure.

## RE of macOS 15.7.8 com.apple.AGXG13G (behaviour notes only; no Apple code copied)
Extracted kernel collection, symbol dumps and annotated disassembly: .local/reference/macos-kernelcache-j313-15.7.8/extracted
(kc/kernelcache.macho, kcscripts/agx.dsyms, kcdis/agx.ann). [E] = read in disassembly/strings, [I] = inference.
- Per MTLCommandQueue a pool of firmware work queues: AGX3DWorkQueue (one TA + one 3D channel each) and AGXCLWorkQueue
  (one CL channel each); defaults (AGXAccelerator::configureDevice): max 2 render work queues, max 3 compute work
  queues, selection mode 3 (dependency-aware) [E]. Blits run on the 3D channel [E].
- Each channel has its own stamp; completion is stamp-based (IOGPUEventMachine, signalStampsUpdated,
  retireCommandBuffer); no notifier lists [E].
- Dependencies: per encoder a wait event merged from the allocation list's resource events and the MTLFences named in the
  kernel command (processComputeSetup / processRenderSetup, mergeEvent, retainMTLFence) [E]; no implicit wait on the
  previous encoder [I]. Queue choice (choose3DWorkQueue / chooseCLWorkQueue): Dependent (a queue whose stamp is already in
  the wait event -> FIFO satisfies it), Idle, Alloc New, Round robin [E]; stripExternalBarriers then removes waits
  already satisfied by the chosen queue [E/I]; remaining cross-queue waits become firmware Barrier commands
  (setupBarrierCommand, writeEventBarrierCommand) [E].
- TVB: render work queues > 0 share work queue 0's parameter management unless configured otherwise [E].
- Consequence for us: Q2c already gives compute its own Queue; the missing piece is emitting the cross-queue Barrier
  only for real dependencies. For D3D12 the dependency source is the application's ResourceBarrier calls (Windows
  contract), not BO identity.

## Designs (offline, 2026-11-05)
- OV2 (KMD): for a flagged independent pair keep the successor's lead Barrier but wait on the stamp value already in
  memory (satisfied at arrival; no validation changes); flag through the existing Asahi per-command header field
  (APPLE_AGX_G4_NATIVE_HEADER CdmBarrier/VdmBarrier = 0xffff = DRM_ASAHI_BARRIER_NONE), gated by a new device feature
  bit; UMD rule v1: both jobs in one barrier-free epoch of the same ECL/context, every driver-internal GPU operation an
  epoch break. Completion when the successor ends first is already handled (runtime BatchedPromotions, in-order
  DMA_COMPLETED). Switch Wom1Ov2NoHazardLead.
- Blocker found: R1/R5/R7 are counted-ExecuteIndirect renders whose v5 prelude (libagx indirect-stream expansion,
  agx_win32_indirect_stream_emit.h) runs on the compute queue; prelude pairs are allowed by default since Q20, but with
  Q2c the prelude queues behind C0 on the compute FIFO, so R1 cannot start before C0 ends even without a Barrier.
- IE1a (UMD only): emit the expansion as its own compute batch right before the render (render becomes a plain v3
  render); IE1b: submit that expansion ahead of C0 and let R1 wait on its stamp (needs OV2's per-job dependency).
- Alternative (KMD, Apple-like): a second compute Queue for preludes (macOS keeps up to 3 compute work queues per
  command queue), so a prelude never queues behind an independent compute job.
- Decision waits on EXP2193 (barriers per boundary; refusal reasons per pair kind).

## OV2 implementation and the query-resolve dependency (2026-11-05, offline)
- OV2 is on integration (820cab01 KMD, e4090c71 UMD, guard below), default off (Wom1Ov2NoHazardLead); the superseded
  subagent branch exp/ov2-rc stays unmerged (its per-batch position tags inspired the early-dispatch guard).
- Found while comparing: EXP2193's render batches are submitted by ResolveQueryData (trigger 'query' at all three
  R->C boundaries). Asahi resolves on the GPU: flush_query_writers submits R, then libagx_copy_query_gl runs in the
  next compute batch (agx_query.c ~480-497). That compute batch reads R's query results, so C depends on R even with
  no application barrier between them. The UMD rule refuses independence after such foreign owner work, so OV2 does
  not fire on GravityMark as is.
- Next design (OV3): take the resolve copy out of the compute job that follows R -- legal to delay until the next
  barrier that names the destination (D3D12: the destination may only be read after a barrier); e.g. emit pending
  resolve copies as their own compute job after C, ordered behind both R (3D stamp) and C. Needs a KMD rule so that
  job waits for R when C was independent.

## OV3: pipeline-statistics resolve copies in the next render's prelude (2026-11-05, offline)
- WINDOWS CONTRACT: ResolveQueryData writes the destination in COPY_DEST; nothing may read it before a barrier that
  transitions it (or an aliasing barrier on its heap), and the CPU sees it only after the queue's fence. A later
  Begin/End on the same index must not change the resolved values.
- AGX/ASAHI CONTRACT: agx_get_query_result_resource_gpu = flush_query_writers + libagx_copy_query_gl in a compute batch
  (agx_query.c ~463-498); a render batch's CDM is the compute prelude the TA waits for (D3D-G1). Asahi prelude header
  barriers 0/0 wait for every earlier render and compute submission; the KMD window gives a prelude successor the
  COMPUTE_ON_3D (behind a render) or COMPUTE_ON_COMPUTE lead Barrier (render_job_window.c AdmissionJobWindowBarrier,
  backend rc102), and a prelude render is never OV2-flagged, so the copy reads finished statistics.
- TRANSLATION (default off: only with the OV2 device feature, i.e. Wom1Ov2NoHazardLead=1 on Q2c, and only in
  executions whose barriers are all logged): ResolveQueryData of a PIPELINE_STATISTICS heap submits the query writers
  now (AgxWin32AsahiQueryResolveFlushWriters) and keeps the 11 copies per query in the owner; after the next draw
  command (direct or ExecuteIndirect) they go into that draw's render batch CDM with AGX_BARRIER_ALL|AGX_PREGFX
  (AgxWin32AsahiQueryResolveIntoRender: the batch has a draw, is not a writer/reader of the query, becomes its reader
  and the destination's writer). Copies still pending go the native way (and the next compute batch is then foreign
  to OV2) at: a transition/aliasing/ranged barrier on the destination heap (or with an unknown heap), a Begin/End of a
  pending query, a non-deferred resolve, the replay end, present and destroy. The OV2 accumulator treats a deferred
  resolve as local (ActiveLocal): the compute batch after R stays independent.
- Cost: no extra job when the next render already has a prelude (RC67 IA statistics put one there while a statistics
  query is active; EXP2193 had ~3 prelude renders of 4 per frame); else one prelude on that render, which already
  waits for C by its own barriers.
- WHAT IS STILL UNKNOWN: whether the R->C overlap then appears on hardware and what it buys (macOS FHD overlap 4.3 ms);
  receipt measure-d3d12-resolve-defer {deferred, into-render, drained, drain reasons} plus Wom1Ov2Receipt waived/refused.
- Checkpoint: one FHD FULL + HD arms with Wom1Ov2NoHazardLead=1 against a cross-boot switch-off control; correct image,
  query values plausible (GravityMark shows its statistics), no fault/TDR.
