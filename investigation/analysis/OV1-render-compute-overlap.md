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

## Why macOS overlaps render and compute (RE, 2026-11-05)
Source: the same kernelcache (com.apple.AGXG13G + com.apple.iokit.IOGPUFamily, macOS 15.7.8). IOGPU call targets
were resolved with kcscripts/iogpu.dsyms, and IOGPU bodies were disassembled with r2 from
kc/entries/com.apple.iokit.IOGPUFamily/__TEXT_EXEC.bin. These are behaviour notes only; no Apple code is
copied. [E] = read in the disassembly, [I] = inference. Addresses are kernel VAs.

### What a submission carries into the kernel
- Per resource: the shared resource list is parsed by AGXAllocationList2::initWithSharedResourceList
  (0xfffffe000909e170, AGXSegmentResourceList / ResourceDescGroup::parseAndValidate). Each entry has a 32-bit
  resource id and a 16-bit usage word [E]. Bits 0x415 are read bits and bits 0x82a are the matching write
  bits; a write bit requires its read bit [E]. Bit 0x80 means residency only [E]. Each usage bit puts the
  resource in its own IOGPUWeakResourceSet at list +0x10 + 8*bit (bit0 +0x10, bit1 +0x18, bit7 +0x48,
  bit10 +0x60, bit11 +0x68) [E]. The middle bits are probably the vertex and fragment stage pairs [I].
- Untracked override: if the resource's byte +0xc1 is set, the entry is forced to 0x80 (residency only),
  whatever usage userland sent [E, 0xfffffe000909e3b0]. That byte comes from bit 35 of the creation flags
  word in IOGPUNewResourceArgs +0x58 (AGXResource::initialize, 0xfffffe000911ee60) [E]. That this bit is
  Metal's MTLHazardTrackingModeUntracked is [I]. The userland side is needed to prove it.
- Per kernel command (encoder): a list of u16 MTLFence ids (AGXHardwareKernelCommand +0x80 count, +0x90
  array), each looked up with IOGPUDevice::retainMTLFence (processComputeSetup at 0xfffffe00090aefb4) [E].
- Per command queue: an accumulated MTLEvent / shared-event wait event at IOGPUCommandQueue +0x4d8. It is
  filled by IOGPUCommandQueue::processKernelCommand from IOGPUMTLEvent::copySubmitted and
  IOGPUSharedEventFence (0xfffffe000ae910a0 / 0xae9120c / 0xae915b0) and merged into later submissions [E].
  The queue also has a second event at +0x580, also used by processSignalSharedEvent and
  processLateEvalWait [E]. Its writer was not found [I: shared-event / late-eval ordering].
- IOGPUEvent = a 32-bit valid mask plus up to 15 slots of (stamp index byte | stamp value << 8)
  (IOGPUEventMachine::setEventStampPair, 0xfffffe000ae686b4) [E]. This is a sparse vector clock over
  firmware channel stamps.

### How the kernel builds a compute encoder's wait (AGXCommandQueue::processComputeSetup, 0xfffffe00090ae8f8)
1. wait = queue MTLEvent waits (+0x4d8) [E].
2. Resource hazards (kernel-side, per IOGPUResource) [E]:
   - for each resource in the write lists (+0x18, +0x68), merge its access event (all earlier reads and
     writes: WAR and WAW);
   - for each resource in the read lists (+0x10, +0x60), merge its write event (RAW), but only once the
     storage has been GPU-written (storage flag bit 15, set by setEventsWithChannel on a write).
   The residency-only list (+0x48) gets only IOGPUCommandDescriptor::addMemory and no event at all
   [E, 0xfffffe00090aff00].
3. Merge the MTLFence wait events [E].
4. chooseCLWorkQueue(wait) (0xfffffe00090aa8d4) picks a firmware CL work queue: Dependent, Idle, AllocNew,
   or RoundRobin (see above) [E].
5. Build the firmware barrier: when the chosen work queue's bit is set in the queue mask at +0x79a,
   stripExternalBarriers(wait, out) (0xfffffe00090b0458) keeps only the stamp slots that belong to this
   command queue's own channels [E]. Those are the TA and 3D channels of each AGX3DWorkQueue (+0x5d8[i],
   count +0x814) and the channel of each AGXCLWorkQueue (+0x5f8[i], count +0x818) [E]. The +0x4d8 and
   +0x580 events are merged back in, and the result goes through IOGPUCommandDescriptor::mergeBarrierEvent
   [E]. If byte +0x7c9 == 1, nothing is stripped (full copy, return 0) [E]. Stamps of other command queues
   or processes are therefore not left in the firmware barrier; they are presumably satisfied before
   submission (late-eval / CPU wait paths) [I]. stripExternalBarriers does not remove any dependency
   inside the queue. It is also reused after submission on the per-storage events, to keep per-channel
   bookkeeping (channel +0x158 / +0x198) [E]. That use is not part of the wait [I].
6. After building the descriptor, the resources in the write lists are stamped with the chosen channel
   (setEventsWithChannel(..., 1)), and the read lists with (..., 0) [E].
- There is no implicit wait on the previous encoder or command buffer in this path [E: the only inputs are
  the four sources above]. processRenderSetup (0xfffffe00090b28b0) uses the same helpers: 19
  setEventsWithChannel calls, including unconditional writes (probably the render-pass attachments), plus
  MTLFence and stripExternalBarriers [E]. Its line-by-line ordering was not traced [I: same model].

### Answer to the four questions
- Dependency info from userland: per-resource usage bits (read/write, per stage, residency-only), MTLFence
  ids per encoder, and MTLEvent / shared-event waits per queue [E]. There are no explicit stamp lists from
  userland. Stamps exist only in the kernel [E].
- A compute encoder may start while the previous render runs exactly when its wait event has no slot for
  that render's TA/3D stamp. Then it lands on a CL work queue with no barrier on the 3D channel, and the
  firmware runs both [E for the mechanism; the concurrency is measured in EXP2192].
- The kernel itself adds BO-level (IOGPUResource-level) RAW/WAR/WAW dependencies, but only for resources
  that userland put in tracked usage lists and that are not flagged untracked (+0xc1) [E]. With a heap, the
  kernel object is the heap allocation [I]. If GravityMark's heaps are untracked, the kernel never sees a
  hazard on them [I].
- stripExternalBarriers removes stamp slots of channels that do not belong to this command queue (other
  queues and processes) from the firmware barrier. It runs on every compute and render setup when the
  chosen work queue's bit is set in the queue mask at +0x79a [E]. It never weakens ordering within the
  queue [E].

### Most likely reason Apple overlaps while our BO check refuses [I, to confirm with mtlhazard]
Metal's contract for untracked resources (heaps default to untracked) is that only the app's MTLFence /
MTLEvent / memoryBarrier calls order encoders. The kernel follows it by reducing such resources to
residency only. So C3/C5/C7 sharing a BO (heap) with the render before them creates no dependency on
macOS. The D3D12 equivalent is exactly the Windows contract already noted above: only ResourceBarrier
orders work. Our Mesa BO-level hazard check corresponds to Metal's *tracked* mode, which GravityMark does
not use on macOS. What only the userland AGXMetal driver (dyld shared cache, not available here) can
answer:
- which Metal hazard mode sets bit 35 of the creation flags;
- whether tracked resources inside one command buffer also go through these kernel lists or are resolved
  in userland;
- how MTLFence update/wait maps to the kernel fence ids;
- whether a concurrent compute encoder changes anything below the kernel.

### Measurement tool (Task 2): mtlhazard.dylib
- Source: scratchpad mtltime/mtlhazard.m (session scratchpad
  /private/tmp/claude-502/-Volumes-pwdev-public-windows/8fb8f1f9-56dc-46ab-b494-4d6264dd5576/scratchpad/mtltime/).
  The README-mtlhazard.md next to it has the record layouts and the `.command` launcher.
- It does not split command buffers. It hooks the concrete classes found at load time, and originals are
  looked up per Method.
- It logs resource and heap creation (storageMode, hazardTrackingMode, heap, offset), command buffer
  new/commit/GPU time, and encoder order and kind with the compute dispatchType. It also logs render-pass
  attachments, MTLFence update/wait (with stages), MTLEvent/MTLSharedEvent signal/wait,
  memoryBarrierWithScope/Resources, and useResource(s)/useHeap(s).
- Per encoder it counts binds split tracked/untracked. With MTLHAZARD_BINDS=1 it also emits the distinct
  bound resources per encoder.
- Build: `clang -fobjc-arc -dynamiclib -framework Metal -framework Foundation mtlhazard.m -o mtlhazard.dylib`
  builds clean (also with -Wall -Wextra). dylib sha256 66cf721fbc4cb6bd... Local smoke test on the dev Mac
  (M4 Pro, a small test program, not the J313): all record types appear and no selector is missing.
- Run (needs the user to reboot the J313 into macOS):
  `MTLHAZARD_OUT=~/gmdiag/hazard-fhd.csv MTLHAZARD_SKIP_CB=600 MTLHAZARD_MAX_CB=300 MTLHAZARD_BINDS=1 DYLD_INSERT_LIBRARIES=~/gmdiag/mtlhazard.dylib <gmdiag GravityMark binary> -temporal 0 -benchmark 1 -metal -asteroids 100000`
  from a .command file launched with `open`.
- Confirms the hypothesis if:
  - the resources bound by c15/c18/c19 and by every compute encoder right after a render are hazard=1, or
    come from an untracked heap;
  - no `fence wait` in those compute encoders matches a `fence update` of the preceding render;
  - no `event wait` lies between them.
  It refutes the hypothesis if those resources are tracked, or if a fence or event links the pair and Apple
  still overlaps it. Then the answer is in userland AGXMetal.

## OV5: compute -> render overlap at the TA stage (2026-11-05, offline, from EXP2201 mtlhazard)
- Apple's mechanism (EXP2201 hazard-fhd.csv, cb 750, scratchpad mtlpairs.py). c15 shares 21 tracked
  resources with R16. Its only memoryBarrier names texture 0x...02d850, and that texture is R16's color
  attachment (rpass attachment 0x36502d850). So R16 does depend on c15, but only for its fragment/3D
  work: Metal lets R16's vertex/TA work run while c15 runs (c15's 1.94 ms lies inside R16's 7.05 ms)
  and orders only the 3D stage. Same pattern for c19 -> R20: c19 binds 0318a0 (R20: fragment-only, m2)
  and barriers bec710. The resources both bind at vertex stage (671370, 677e50, 78ea10, 790320,
  72ca00) are not named by c15's barrier, i.e. read-read.
- WINDOWS CONTRACT: R after C with no barrier naming a range both access, where one writes, is
  unordered. With a transition between them (C UAV-writes X; X -> RENDER_TARGET or -> PIXEL_SHADER_
  RESOURCE), R must not access X before C's writes complete. Nothing requires R's vertex work, which
  does not touch X, to wait.
- AGX/ASAHI CONTRACT: a render job is a TA (VDM) part and a 3D part in separate firmware
  microsequences. Today the KMD window gives the whole render a TA_ON_COMPUTE lead barrier, so the TA
  waits for C. The 3D part runs after the TA by construction. A counted-ExecuteIndirect render's prelude
  (v5 expansion, IA statistics, OV3 copies) runs on the compute queue behind C, and the TA waits for it
  (D3D-G1b).
- TRANSLATION:
  - UMD, per render R after compute C. Track C's accessed ranges, the same way the ranges rule tracks a
    render's, from ReplayDispatch bindings with UAVs marked as writes. Split R's accesses by stage:
    vertex = VB, IB, indirect args/count, SO, and VS views; fragment = RTV, DSV, PS views and absorbed
    clears.
  - Flag R as "TA independent" when all of these hold:
    - C's UAV-written ranges are disjoint from R's vertex ranges;
    - C's accessed ranges are disjoint from R's vertex-stage writes (SO, VS UAVs);
    - no global UAV barrier sits in the gap while both use UAVs;
    - nothing is unplaceable;
    - R has no compute prelude.
  - KMD: for a flagged render, waive the TA lead wait on C's stamp, but put the wait on C's stamp into
    the 3D microsequence. The 3D always waits, so fragment-stage dependencies (c15 -> R16 attachment)
    stay ordered without tracking.
- Blocker for our C5 -> R6: R6 (4 counted-EI draws) has a compute prelude, which queues behind C5 on
  the compute FIFO. Only OV5 plus moving that expansion ahead of C5 (IE1b: it reads args C3 wrote;
  Metal c13 writes R14/R16's indirect buffer 79ae60) ports c15/R16. Our C7 -> R8 (6 direct draws, no
  prelude under OV3b) ports with OV5 alone: c19/R20 is 1.45 ms FHD / 0.72 ms HD on macOS, and our
  C7 is 0.65 ms HD.
- WHAT IS STILL UNKNOWN:
  - Whether the firmware 3D microsequence can wait on a compute stamp: KMD/firmware work for fork 6,
    with m1n1 or Asahi as reference.
  - Which of our 17 C5 -> R6 gap barriers / 2 C7 -> R8 gap barriers name C's ranges. A receipt run
    with the C-range tracker will show it.
  - Whether hoisting R6's expansion ahead of C5 is legal per frame. Count/args writers would have to
    be C3 only; that needs a receipt.

## OV4 parked (2026-11-05)
- EXP2201 shows Apple never overlaps render->compute, so OV4 (split a compute batch after the preceding
  render and flag its prefix independent) is parked: code dbaf4b68, opt-in APPLE_AGX_D3D12_OV4, never run.
- BLOCKER (fork 6 review): after a split, the suffix S follows the waived prefix P. If P completes before
  the render R, the depth-two window could kick S behind P alone (COMPUTE_ON_COMPUTE) while R still runs.
  S would then need its own render-stamp wait. Not resolved; resolve it before OV4 is ever revived.

## OV5 UMD -> KMD flag (proposal, 2026-11-05)
- Device feature AGX_WIN32_DEVICE_FEATURE_RENDER_TA_INDEPENDENT = 0x4: the KMD publishes it only with its
  own switch, and only when it can put the compute-stamp wait into the 3D ring.
- Flag: a render packet without a compute prelude command whose render header carries cdm_barrier =
  DRM_ASAHI_BARRIER_NONE (vdm_barrier 0). Today a plain render sends (0, 0) and a prelude render sends
  (NONE, 1) on the render header after its compute command, so CdmBarrier = NONE on a render with no
  compute command in its packet is unambiguous and cannot be confused with OV2 (VdmBarrier = NONE on a
  compute envelope).
- KMD meaning (not Asahi's): the TA does not wait on the compute stamp; the 3D ring waits on it. Without the
  feature the parser refuses it.

## OV5 KMD drill (step 1, 2026-11-05, offline; no hardware run yet)
- Sources: Asahi `queue/render.rs` (frag Barrier: `wait_stamp` = vertex fw stamp, `stamp_self` =
  `ev_frag.value.next()`, added to the frag job) and `queue/mod.rs` (explicit vdm/cdm barriers go only
  into the vertex or compute job); our `AdmissionBuildLeadBarrier`, `AppleAgxG13ProviderSubmitRender`,
  `AppleAgxG13QueueRuntimeSubmit`, `AppleAgxG13QueueProviderPlanSuccessor`, the sidecar layout in
  `apple_agx_render_shared_memory.h`.
- WINDOWS CONTRACT: none changes. The drill adds no D3D12/WDDM-visible ordering: the render already waits
  for the compute job at its TA. VidSch sees the same fences in the same order.
- AGX/ASAHI CONTRACT: a G13 Barrier work item (tag 4, 0x20 bytes) in a work queue holds that queue until
  `*wait_stamp >= wait_value`. `stamp_self` is the waiting queue's own next stamp. Asahi never puts a
  compute-stamp Barrier into the 3D (fragment) queue. The 3D queue's only Barrier is the one on the TA
  stamp (our template object 14, D3 entry 0).
- TRANSLATION (REG_DWORD `Wom1Ov5FragWaitDrill` = 1, default off, read at StartDevice; `ADMISSION_OV5_ON`
  in `render_ov5.h`):
  - Scope: a successor render with lead class TA_ON_COMPUTE and no compute work of its own. It keeps its
    TA lead Barrier unchanged.
  - The KMD also builds a second Barrier with the same wait: compute fw stamp (sidecar 0x8780),
    `wait_value` = the running job's ComputeExpectedStamp, `wait_slot` = ComputeEvent, `stamp_self` = the
    render's D3ExpectedStamp.
  - It lies in a new sidecar slot, `APPLE_AGX_COMPUTE_3D_BARRIER_OFFSET` = 0x8840 (set B 0xa840), in the
    render's job-word set. No other word of either set overlaps it; `test_ov5_frag_wait_drill`
    SidecarSlot checks every extent.
  - The provider puts it first in `submission.D3`, with the sidecar as its prepared range
    (`D3LeadBarrierRange`, flushed before the 3D work), and sets `D3LeadingBarrier`. The runtime accepts
    3 D3 entries only for such a successor.
  - The plan's `D3BarrierExpectedDonePointer` (one more 3D entry) becomes the job's 3D done pointer.
  - Refusal is never truncation: the pure `AdmissionJobWindowOv5DrillRefusal` checks the switch,
    successor, class, compute work, D3 entries < `APPLE_AGX_G13_QUEUE_PUBLISH_MAX` and an in-ring done
    pointer. A failed encoder or sidecar reservation also falls back to today's job. The provider rejects
    the field on depth-1, compute-work and no-TA-lead jobs.
  - Receipt `Wom1Ov5DrillReceipt` (v1, 68 bytes): Considered, Built, Placed (provider count), Refused[8],
    last fence/wait/stamp_self/offset. Exported like Wom1Ov2Receipt: PASSIVE_LEVEL, at most every 2 s,
    never flushed.
  - Switch off: the provider probe's publication (rings, write pointers, flushed ranges, run messages) is
    byte-identical to base 92dfda88f.
- WHAT IS STILL UNKNOWN (hardware only):
  - Does the G13 firmware accept a Barrier on the compute stamp in the 3D queue? Possible failures: a
    firmware assert/RTKit crash, a channel error on the 3D queue, or a hang because the 3D queue
    resolves stamps differently. The wait is always satisfied on arrival, so a hang would mean the
    firmware rejects or misreads the item.
  - Does a satisfied 3D Barrier ahead of the TA-stamp Barrier change 3D start latency?
  - Firmware writes into the sidecar's shared statistics region (0x8600; its real extent is undocumented
    here; Asahi's global GpuStatsComp is 0x3000 bytes) would reach 0x8840 as they would the
    hardware-run 0x8800/0x8820 Barriers. That is no new exposure, but it is not proven.
- Checkpoint (smallest falsifiable): one GravityMark FHD run with `Wom1JobDepth2Kick` = 1 and
  `Wom1Ov5FragWaitDrill` = 1, all else as the current default package.
  - Pass: `Wom1Ov5DrillReceipt` Built == Placed > 0 with Refused[7] (build) = 0; no TDR, firmware
    recovery or channel error; frame time within run-to-run noise of the switch-off control.
  - Fail: any firmware fault or hang with Placed > 0. Recovery: the switch off (REG_DWORD 0 / delete),
    the standard recover.sh tail.
- Step 2 sketch, the real waiver (not implemented):
  - The KMD publishes device feature 0x4 `AGX_WIN32_DEVICE_FEATURE_RENDER_TA_INDEPENDENT` only behind
    `Wom1Ov5TaWaiver` = 1, and only when the drill passed on hardware.
  - The UMD flags a render as TA-independent per the OV5 rule above: render header `cdm_barrier` =
    0xffff (NONE), `vdm_barrier` = 0, a packet without a compute command.
  - For such a successor (TA_ON_COMPUTE, no compute work) the KMD drops the TA-ring lead Barrier: TA
    publication = root only, TA done pointer = the plan's two-entry value without the lead.
  - It keeps only the 3D Barrier above. The 3D queue then waits on both C's stamp and its own TA.
  - Refusals: the switch off, feature unpublished, packet not flagged, class/compute work. Counted in a
    new reason of the same receipt. Without the feature, the parser refuses the flag.
  - Open for step 2: `AppleAgxG13ProviderTaValid` and `PlanSuccessor` count the TA lead as one entry.
    The waiver needs a successor TA publication without one, which the runtime's
    `Ta.GpuAddressCount == 1 + successor` check forbids today, and a backend-job validity rule other
    than `LeadBarrierAddress != 0`.
