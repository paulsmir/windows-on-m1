# R161 — EXP874: R159-admitted uploads race deferred paging (operator Claude)

## Hardware facts (EXP874, package 874 = 9394b99f = EXP872 717b4bef + R159 75c37a11, no R158)

- Code0 at 68 s (DWM 1236, Explorer 5208), then 0x116 (p3 0xC0000483); dump in hidden recovery.
- UMD log: reject-copy-slot step2 x4 (R158 class, expected without R158), reject-batch x7,
  reject-private-escape x3 (hr 0x8876017C, private scene pool).
- Kernel dump, context ffffbc85cfa02000: Completed 0x453, Active 0x454, preemption WaitCurrentBoundary.
  RenderPacket fence 0x454: DestinationGpuVa 0x1f0000, 0xFA0000 bytes (2560x1600x4), local physical
  0x8e1170000 — an ordinary full-screen GPU-local target, not a presentation surface.
  Backend: Phase Submitted, TaComplete 0, D3Complete 0 (same signature as EXP873).
- EXP872 (same base without R159) was stable past 600 s with ~740 copy step3 (predicate 45) refusals.

## Verdict on the EXP873 bisect

R158 is not required for the hang; R159 alone reproduces it. The hang follows the work that predicate
45 used to refuse: UPLOAD/DOWNLOAD after the mapping generation changed since QUERY.

## Mechanism (source)

- `AdmissionGpuvaG3BuildPagingBuffer` applies UPDATE_PAGE_TABLE to the logical graph at build time
  (`gpuva_g3_paging_windows.c`, ResidentPtes/LogicalPtes, ++MappingGeneration).
- FILL/TRANSFER (physical, `paging_windows.c` AdmissionEncodePaging) and VIRTUAL_FILL/TRANSFER
  (`AdmissionG3EncodeVirtualPaging`) are only encoded; they execute later in `AdmissionPagingWorker`,
  dispatched only after the active render fence completes (node 0 serialized).
- DxgkDdiEscape with HardwareAccess is second-level synchronized (MS Learn DXGKDDI_ESCAPE), which does
  not idle the GPU or the driver's paging worker.
- Therefore a copy escape between build and execution resolves the new placement, writes the upload
  there, and the pending TRANSFER/FILL later overwrites it (DOWNLOAD reads before the move). Shader,
  vertex or control data then contains stale/zero content; the GPU job never completes (no fault).
- Predicate 45 accidentally hid this by refusing every copy after a mapping change.

## Fix R161 (17c8fc2c)

`PagingRecordsUnsubmitted` counts encoded records until SubmitCommand hands them to the CPU queue.
Copy UPLOAD/DOWNLOAD waits (lock released, bounded 3 s like R157) until no record is unsubmitted,
queued, pending or executing; otherwise predicate 62 STATUS_DEVICE_BUSY without touching memory.
QUERY does not wait. Replay: RED (upload proceeds without waiting) -> GREEN, profiles 16/64.

## Remaining (not addressed here)

- Private scene pool exhaustion (reject-private-escape 0x8876017C) still kills some devices.
