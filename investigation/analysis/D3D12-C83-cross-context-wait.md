# D3D12 C83: cross-context completion bridge (offline candidate)

## WINDOWS CONTRACT

Pinned WDK 26100 primary sources (under `.local/reference/wdk26100`):
- `d3d12umddi.h:2660`: the runtime supplies D3DDDI_DEVICECALLBACKS.
  `:2706-2737`: queue Signal/Wait DDIs receive fence placements, not kernel
  synchronization handles. The measured runtime owns those signals (C3b,
  `agx_d3d12_ddi.cpp:3382-3389`); do not implement guessed fence-write packets.
- `d3dumddi.h:3839-3850`: CPU wait accepts a completion event; NULL would
  block inside the callback. `:3859-3876`: GPU wait names a context and an
  array of monitored fence values. `:3895-3920`: GPU2 signal names its
  broadcast contexts and monitored values. Flags stay zero (ordered signal).
- `d3dkmthk.h:5048,5084`: corresponding kernel thunk contracts.
- `d3dkmddi.h:1994-2051,4618`: software scheduling capabilities and
  DXGK_OPERATION_SIGNAL_MONITORED_FENCE. GpuMmu does not require hardware
  queues for these context-based callbacks.

Option A is the smaller supported translation: reuse the owner's existing
private monitored render fence. After flush has submitted and signalled V,
queue a wait for V on the *application queue context*, using the KT device runtime handle (the callback's hDevice argument). A subsequent runtime Signal/Present on that context must
follow that wait. Do not confuse V with KMD SubmissionFenceId or invent V+1.
Option B is possible in principle, but replacing just KernelContext is not:
context creation associates callbacks with a runtime queue, and the owner's
paging, transfers, retirement and resource tracking currently own a separate
context. Moving the entire owner per queue is a larger change.

The reverse edge remains a baseline limitation: an earlier application queue
Wait does not gate owner CPU replay. A CPU ingress gate is **not** a correct
small fix: the legal single-thread sequence B.Wait(F,1), B.Execute(consumer),
A.Execute(producer), A.Signal(F,1) must enqueue without blocking B.Execute.
A GPU-only ingress wait cannot order CPU texture copies or indirect expansion.
Full incoming-wait support needs deferred replay with retained command/resource
lifetimes (or a queue-owned submission design). C83 does not claim that support;
it is an experimental outgoing completion bridge, preserving the existing
replay behavior and default-off until hardware validates the narrower edge.

## AGX/ASAHI CONTRACT

Existing source evidence, before this change:
- `umd_runtime_device.c:463-497`: private monitored render fence, initial 0.
- `umd_gpuva_windows.c:1326-1337,1552-1564`: signal after SubmitCommandCb.
  `:1408-1433`: asynchronous no-download completion or synchronous staging
  download plus a second signal. Use the returned fence, never an inferred one.
- `agx_win32_gpuva_batch.c:841-848,1005-1007`: records the newest accepted
  completion fence. `agx_win32_asahi_scene.c:197-210`: retire polls and
  releases batches; it is more than an OS fence wait.
- Mesa `src/gallium/drivers/asahi/agx_pipe.c:1796,1817`: flush and server-sync
  entry points; Windows uses its own batch adapter and monitored fence, not
  Linux DRM syncobj handles.
- `lifecycle.c:738-744,766-769`: one node, MultiEngineAware, boundary
  preemption, GpuMmu. `driver.c:100`: SubmitCommandVirtual registered.
- `gpuva_g3_windows.c:4442-4535`: virtual submission validates context and
  routes paging/present/render. `gpuva_g3_paging_windows.c:933-935,988-990,
  1173-1182`: monitored-fence paging records write the value.
- `scheduler_windows.c:218-244,272-305`: completion and DPC accounting.
  There is no KMD GPU-wait opcode to add: VidSch owns the blocked context
  dependency, while KMD owns actual packet completion. This interaction is
  a hardware checkpoint, not a host-test claim.

The inspected firmware boundary is unchanged: no m1n1/Mu/MMIO/IRQ/DMA/power
change is proposed, and no live state is queried for this offline task.
KMD retains initialization, interrupts, GPU DMA and recovery; UMD owns replay,
private fence lifetime and resource retirement; VidSch owns queue ordering.
No external implementation code is copied.

## TRANSLATION

Implementation plan and result (inline; end-to-end work authorized):
1. Add a WDK callback helper, exercised as compiled C++ on the host. Pass the
   KT **device** runtime handle (not the HRTCOMMANDQUEUE core-layer handle),
   the waiting queue context, existing owner render sync handle and exact
   64-bit accepted completion value. No new fence, increment, truncation or
   per-owner deduplication: another queue must wait even on the same value.
2. Sample `APPLE_AGX_D3D12_GPU_WAIT=1` exactly at queue creation. Require
   pfnWaitForSynchronizationObjectFromGpuCb; absent/other values stay on CPU.
   Execute validates queue/list device identity, holds the shared owner lock
   across replay and submit, and passes the actual queue context to the owner.
3. Flush, check fault state, enqueue the wait on the newest render fence.
   Only S_OK skips retire. A zero fence, missing callback, failed callback or
   fault falls back to CPU retirement; a failed retire/fault returns device
   hung. Explicit SubmitAndWait users (maps and sparse remapping) remain
   synchronous; presentation preparation is unchanged. Teardown now drains
   even without pending presentations.
4. Receipts: queue-sync {requested,enabled}; queue-wait {armed,fence-low,
   fence-high}, status = callback result; existing C82 time receipts remain.
   Successful owner-submit means ordered completion, not CPU retirement.
5. RED/GREEN regression, ABI/native-owner checks, full baseline, independent
   review, small commits and handoff report. No ledger edits in this branch.

No KMD change is indicated by the inspected source. VidSch holds the waiting
context; the existing KMD reports real completion. No firmware, capabilities,
signer, package, trim or residency changes. No external code was copied.

## WHAT IS STILL UNKNOWN

- Actual Windows runtime callback acceptance for a queue waiting on the
  owner's private fence, and VidSch progress/fairness on this one-node KMD.
- Incoming application queue waits still bypass owner replay, as in the
  baseline. This is not a fully correct D3D12 cross-queue implementation.
- Performance: batch-slot reuse, dependency sync and lower winsys submission
  waits can absorb time formerly spent in retire. Only total frame time can
  prove overlap improved; a shorter end wait alone is insufficient.
- Windows compilation and hardware behavior are not proven by host tests.
- Async retirement can expose lifetime/pressure bugs previously hidden by
  eager retirement; mapping, batch slot reuse and teardown remain CPU waits.

## Checkpoint

Claude should first run a producer Execute on queue A followed by a runtime
Signal, CPU-await the signal and verify readback contents. Repeat with queue B
using the same owner, checking each signal against that queue's submitted work.
Include multiple batches per Execute, repeated/empty Executes, CPU readback,
present and create/destroy without Present. A later incoming-Wait repair must
also test same-thread future producer submission; do not call that solved here.
Then run Night Raid GT1 with C82 receipts, option off/on on the same package.
Compare total Execute and frame time, GPU completion, output and errors. The
optimistic ceiling is roughly 134 -> 81 ms (C82's full-frame span was ~146 ms);
it is a hypothesis, not a host-test result. Fail on early app fence/readback,
timeout/device removal, teardown failure or no net improvement. If GT1 depends
on an incoming wait across queues, this candidate cannot establish correctness.
Recovery: turn the option off and use the established exact-package GPU-visible
recovery procedure. No checkpoint was run here.
