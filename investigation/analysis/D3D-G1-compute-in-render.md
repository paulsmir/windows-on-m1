# D3D-G1: compute work inside a render submission (2026-10-14)

This is the first item of the D3D groundwork (D3D-phase-plan.md, step 3). Mesa's Asahi driver puts
compute (CDM) work into a render batch for:

- geometry shaders, including the passthrough GS used for stream output (`agx_launch_gs_prerast`);
- tessellation (`agx_draw_patches`);
- input-assembler statistics (`agx_ia_update`);
- primitive-restart unrolling (`agx_draw_without_restart`);
- internal precompiled kernels (`agx_launch_internal`/`agx_launch_precomp`);
- in-place decompression (`agx_decompress_inplace`).

The current G4 GPUVA path refuses every such batch. D3D10 stream output and general GS (FL 10_0),
tessellation and SM5 (FL 11_0), and GL 3.2+ all depend on it.

## Sources inspected

**Asahi Mesa (reference tree):**
- `gallium/drivers/asahi/agx_state.c`: lazy `batch->cdm` allocation in a render batch at
  3832/4060/4205/4529/5481.
- `agx_batch.c` `agx_batch_submit`: a compute command, then the render command with
  `agx_cmd_header(false, compute ? DRM_ASAHI_BARRIER_NONE : 0, compute ? 1 : 0)`, i.e. the render
  waits on the submission's compute command.

**Asahi kernel:**
- `drivers/gpu/drm/asahi/queue/mod.rs:787-820`: for a header barrier index, the kernel adds a
  firmware `Barrier` work item to the waiting queue (vertex queue for a render). The item has:
  - `tag` = `CommandType::Barrier` (4);
  - `wait_stamp`, `wait_value`, `wait_slot` = the compute event's firmware stamp, value and slot;
  - `stamp_self`, `uuid` 0xffffbbbb, `external_barrier` 0, `internal_barrier_type` 1;
  - 64 bytes in total (`fw/workqueue.rs:33-44`).

**This tree (read-only survey):**
- `shared/src/apple_agx_g4_submit.c` `parse_submit`, lines 112-217: optional attachments, then
  exactly one RENDER command with both barriers 0. A COMPUTE command is Unsupported.
- `AppleAgxG4ParseComputeEx` (:278) admits only a standalone compute envelope (v4).
- `render-admission/src/gpuva_g3_windows.c` handles the envelope types as mutually exclusive:
  - `AdmissionG4SubmitVirtualEnvelope` (:3941) branches on IsCompute (:4025) or on a v3 lease
    with ParseSubmitEx (:4092);
  - `AdmissionGpuvaG3BeginJob` re-parses either G4Native or G4Compute (:2660-2702);
  - `AdmissionG4PendingQueue` enforces exactly one kind (:3404-3415).
- `render-admission/src/render_backend_image.c`: `BindG4Compute` refuses when G4Native is set
  (:392), and `StageJob` refuses G4Compute (:725).
- The queue layer already runs a compute prelude before a render (CPU-sequenced):
  - `apple_agx_g13_queue_provider.c` `RunTa` with `ComputeWorkAddressCount != 0` submits the
    compute first and stages the TA/3D submission (:362-427);
  - on the compute event, `BeginRenderAfterCompute` submits the staged render (:678-697).
  - Tests: queue runtime `TestComputeThenRenderDependency`, queue provider
    `TestComputeCompletesBeforeRenderPublication`.
  - Its only caller is the legacy MIXED_BATCH path (`backend_platform_windows.c:2473-2502`).
- The job image carries both kinds of work: `ComputeWorkAddresses[0]`, `ComputeEvent`,
  `ComputeExpectedStamp` and `ComputeExpectedDonePointer` on a Kind=Render job.
- Winsys `mesa/winsys/agx_win32_gpuva_batch.c` refuses a render batch with `cdm.bo`:
  - in `ComputeEnter` (:270);
  - in `ComputeFinalize` (:287);
  - in `Finish` (:459, kind 2 bit 9).
  - `publish_shadows` (:420) publishes one encoder only.
  - The Mesa overlay (`native-asahi-batch-lifecycle.py:137-141`) passes only the render command
    to `Finish`.

## Contracts

**WINDOWS CONTRACT:**
- One `DxgkDdiSubmitCommandVirtual` packet is one DMA buffer with one fence.
- The KMD may execute it as several GPU commands. The fence completes when all of them are done,
  or the packet is reported faulted as a whole (G4-r2b).
- Preemption stays at the DMA-buffer boundary (`PreemptionCaps` `DMA_BUFFER_BOUNDARY`): the
  compute and render of one packet are never split by a preemption.

**AGX/ASAHI CONTRACT:**
- A submission may carry compute and render commands.
- A render that consumes compute output waits on the compute command's stamp:
  - through an explicit firmware Barrier work item on the vertex queue (Asahi);
  - or, equivalently, by publishing the render only after the compute stamp is observed (this
    KMD's queue provider, CPU-sequenced).

**TRANSLATION:**
1. Envelope v5 = the v3 render envelope (private VA, scene lease) plus the compute preemption
   range of v4.
2. Its DMA buffer is the Asahi command stream:
   - COMPUTE header + `drm_asahi_cmd_compute`;
   - optional attachments;
   - RENDER header with `vdm_barrier` NONE and `cdm_barrier` = 1 (it waits on the first CDM command, the
     compute) + `drm_asahi_cmd_render`.
3. Parser rules: one COMPUTE then one RENDER; the render must name the compute as its CDM barrier;
   any other barrier, a second command, or trailing bytes fail closed.
4. The KMD binds both payloads to one backend image and builds a Kind=Render job whose `Compute*`
   fields hold the compute work item. The queue provider runs the compute first, then TA/3D
   (existing CPU-sequenced prelude). The fence completes on 3D.
5. The winsys publishes both encoders' shadows and composes v5 for a render batch that has a CDM
   encoder.

**WHAT IS STILL UNKNOWN:**
- (a) Whether the compute prelude path, so far exercised only by the legacy MIXED_BATCH,
  completes on hardware through the GPUVA job builder.
- (b) The cost of the CPU round trip between compute and render. If it is significant, D3D-G1b
  replaces it with the firmware Barrier work item; that is a separate, measured variable.

## Steps (each with tests; one hardware checkpoint at the end)

1. **Offline, shared:**
   - `AppleAgxG4ComposeHeaderV5`, `AppleAgxG4ParseSubmitWithComputeEx`;
   - RED to GREEN parser tests: compute + render admitted; a render without the barrier, a
     barrier other than 1, two computes, compute after render, and trailing bytes refused.
2. **Offline, KMD:**
   - v5 dispatch in `AdmissionG4SubmitVirtualEnvelope` and BeginJob;
   - pending entry and backend image binding that carry both;
   - the job builder fills `Compute*` for the prelude;
   - tests: `g4_submit_virtual_replay` and `render_backend_image` cases.
3. **Offline, winsys and overlay:**
   - accept CDM work in a render batch and publish both shadows;
   - compose v5;
   - the overlay passes the compute command too;
   - test: `test_g2_winsys_compute_envelope`-style admission of the composed v5 by the KMD parser.
4. **Hardware checkpoint (one variable: render batches may carry compute):**
   - a D3D11 FL 10_0 test with a geometry shader and stream output (new `agx_gs_smoke`), read
     back and verified;
   - every existing check unchanged (CS, D3D11 smoke, KMT, BIG, pressure, long-job).
   - Recovery: the standard rollback.

## D3D-G1b: the firmware Barrier (after EXP1278)

EXP1278 showed that the CPU-sequenced prelude deadlocks. The KMD submitted the compute and waited for its event
before publishing TA/3D, but the event never came. The Queue notifier is shared by TA, 3D and compute, and its
threshold already counts this job's TA and 3D notifications (`threshold = NotifierRenderCount +
NotifierComputeCount`). The firmware signals only when the threshold is reached, so the result was TDR → 0x116.

**Asahi (primary source):**
- `queue/mod.rs` sets `notification_count = 2*nr_render + nr_compute` and raises the threshold once when the job
  runs.
- Every queue of the submission is kicked at once.
- A render that waits on a compute command gets an explicit `fw::workqueue::Barrier` on its vertex queue:
  - `wait_stamp`, `wait_value` and `wait_slot` come from the compute event;
  - `stamp_self` is the vertex job's own next value;
  - `uuid` is 0xffffbbbb.
- The fragment queue's barrier on the vertex stamp is the same pattern: `stamp_self = ev_frag.value.next()`.

**G13 layout (captured `WorkCommandBarrier`, render template object 14, 32 bytes):**

| Offset | Field |
|---|---|
| 0x00 | tag 4 |
| 0x04 | wait stamp pointer (u64) |
| 0x0c | wait value |
| 0x10 | wait slot |
| 0x14 | stamp_self |
| 0x18 | uuid |
| 0x1c | external |

There is no G14X `internal_barrier_type`.

**Translation:**
- `AppleAgxRenderSharedMemoryBuildComputeBarrier` writes the barrier at compute sidecar +0x8800:
  - it waits on the compute work's firmware stamp (sidecar +0x8780), with value = the compute stamp value and
    slot = the compute event;
  - `stamp_self` is the TA job's stamp.
- The job builder sets `Job->TaBarrierAddress`, and `TaExpectedDonePointer` comes from the plan's
  `TaBarrierExpectedDonePointer` (one more TA ring entry).
- The provider leads the TA list with the barrier; its prepared range is the sidecar, which the compute
  publication flushes.
- `AppleAgxG13QueueRuntimeSubmitRenderWithCompute`:
  - publishes compute, 3D and TA, then sends their Run messages in that order;
  - the fence completes when TA, 3D and compute are all done.
- Compute work without the barrier, or a barrier without compute, is refused:
  - backend job validity;
  - provider guard `AppleAgxG13ComputeGuardBarrier`;
  - `Submit`/`SubmitCompute` refuse `TaLeadingBarrier`.
- The CPU-sequenced path (`BeginRenderAfterCompute`, `ComputeComplete`) is removed.

**WHAT IS STILL UNKNOWN:**
- Whether G13 firmware accepts a barrier on the TA queue that waits on a compute stamp; macOS's captured barrier
  waits across the TA → 3D queues, Asahi's across compute → vertex.
- Whether the compute firmware stamp holds the raw stamp value the barrier compares.

**Checkpoint:** `agx_gs_smoke` gs/so on both the x86 and ARM64 builds. Recovery: the standard rollback, dump first
on a bugcheck.
