# D3D12 C3: command queues and fences

Status 2026-10-17. C1 (EXP1355) and C2 (EXP1359) are proven: a D3D12 device
exists and a committed upload buffer maps and round-trips. C3 is the queue,
its fences, and an empty ExecuteCommandLists.

## WINDOWS CONTRACT

- d3d12umddi.h (pinned WDK 26100), device core 0003:
  `pfnCalcPrivateCommandQueueSize` / `pfnCreateCommandQueue`
  (`D3D12DDIARG_CREATECOMMANDQUEUE_0001`: hDrvCommandQueue, hRTCommandQueue,
  QueueFlags, NodeMask) / `pfnDestroyCommandQueue`;
  `pfnCalcPrivateFenceSize` / `pfnCreateFence` / `pfnDestroyFence` with
  `D3D12DDIARG_CREATE_FENCE { FenceCount, D3D12DDI_FENCE *Fences }`,
  `D3D12DDI_FENCE { FenceValue, FenceMonitoredValue (GPU VA placements),
  Flags }`; queue table 0001: `pfnSignalFence(hQueue,
  D3D12DDIARG_FENCE_OPERATION*)`, `pfnWaitForFence`, `pfnExecuteCommandLists`.
- Microsoft, "Context monitoring" (WDDM 2.0): a monitored fence has a
  read-only CPU VA and a read/write GPU VA; "To signal the fence from the GPU,
  the UMD inserts a fence write command in a context command stream"; "When
  a command buffer completes execution on the GPU, Dxgkrnl goes through the
  list of fence objects with pending waits ... reads their current fence
  value". A GPU that cannot write the fence uses
  SignalSynchronizationObjectFromGpuCb (needs the kernel sync handle, which
  the D3D12 fence DDI does not give the driver). GPU waits:
  WaitForSynchronizationObjectFromGpuCb after a flush.
- DXGK_VIDSCHCAPS::No64BitAtomics declares 32-bit-only atomic fence updates.

So a queue signal = a write of the 64-bit value at FenceValue's GPU VA,
ordered after the queue's earlier work, observed by Dxgkrnl at the next DMA
completion (basic monitored fence; no monitored-fence interrupt needed).

## AGX/ASAHI CONTRACT

- Asahi (drm/asahi queue + firmware): a job's completion is reported by the
  firmware stamp/event; the kernel driver, not the GPU shader, owns
  completion bookkeeping.
- This KMD already implements the exact write a basic monitored fence needs,
  for paging: DXGK_OPERATION_SIGNAL_MONITORED_FENCE records
  (gpuva_g3_paging_windows.c) resolve the fence GPU VA through the process
  page tables (AdmissionG3ResolveLogicalVa) and write the value when the
  paging operation completes, then report DXGK_INTERRUPT_DMA_COMPLETED
  (paging_windows.c).
- Render submissions already complete through the KMD's fence path (D3D11:
  DMA completion + the UMD's SignalSynchronizationObjectFromGpu2Cb on its own
  monitored fence).

## TRANSLATION

1. CreateCommandQueue -> one kernel GPU context per queue
   (pfnCreateContextVirtualCb, the D3D11 device's GPUVA context private data)
   and a queue record (the runtime handle, a list of pending fence writes).
2. CreateFence -> records each D3D12DDI_FENCE's FenceValue GPU VA (and the
   monitored value VA, unused by a basic implementation).
3. SignalFence(queue, fence, v) -> a submission on the queue's context whose
   DMA private data is a "monitored fence write" record {VA, value}; the KMD
   resolves the VA like the paging record and writes v when the submission
   completes (after every earlier submission on that context), then reports
   DMA completion; Dxgkrnl re-reads the fence. No GPU work is executed.
4. WaitForFence(queue, fence, v) -> WaitForSynchronizationObjectFromGpuCb is
   not usable without the kernel handle; first measure whether the runtime
   implements queue waits itself for 0003 drivers (it holds the sync object),
   before implementing a KMD-side wait.
5. ExecuteCommandLists with zero lists -> no submission (nothing to order).

## WHAT IS STILL UNKNOWN

- Which DDIs the runtime calls for ID3D12Device::CreateCommandQueue and
  CreateFence on a 0003 driver (receipts: refused DDIs are named).
- Whether the runtime itself creates and signals the kernel monitored fence
  for a 0003 driver's queue signal (in which case pfnSignalFence may only
  need ordering), or relies on the driver's GPU-VA write.
- Whether the KMD's render submission path accepts a submission with no AGX
  work and only a fence-write record (new envelope kind; RED -> GREEN parser
  test first).

## Checkpoints

- C3a (hardware, receipt-driven like C1): probe creates a direct queue and a
  fence, calls Signal(fence, 1) and GetCompletedValue/SetEventOnCompletion;
  receipts name each DDI the runtime asks for.
- C3b: implement the measured set (queue object + context; fence records;
  KMD fence-write envelope with an offline parser test); checkpoint
  GetCompletedValue() == 1 after Signal.
