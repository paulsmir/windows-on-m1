# R160 — EXP873: batch refusals gone, first presentation-surface render hangs the GPU

## Hardware facts (EXP873, package 873 = f2dda909 = EXP872 717b4bef + R158 c05ab68c + R159 75c37a11)

- Code0 at 79 s (DWM 1228, Explorer 5148), then 0x116 VIDEO_TDR_FAILURE (p3 0xC0000483) at ~100 s.
- UMD log (hidden collection, 510 lines): **zero** reject-batch / reject-copy-slot / reject-private-escape
  records. R158 (direct presentation) and R159 (copy revalidation) removed every refusal class seen in
  EXP871/872; batches now reach the KMD and the firmware.
- Kernel dump (Kernel-MEMORY.DMP, EXP873-direct-present/hardware-evidence), adapter context
  `ffff8107c5a2d000` (found via !drvobj AppleAgxAdmission 7 -> FDO ffff8107c5a25030 -> DevExt
  ffff8107c5a25180 -> page-aligned pointer):
  - Scheduler: Completed 0x141, Active 0x142, preemption pending (WaitCurrentBoundary), SchedulerFaulted 1.
  - RenderPacket Active fence 0x142: DestinationGpuVa 0x1060000, 4 MiB, DestinationPhysical 0x8e1450000
    (local reserve) — a presentation surface now rendered directly (R158).
  - Backend runtime: Phase Submitted, TaComplete 0, D3Complete 0 → the job reached firmware and neither
    TA nor 3D completed. G3 ActiveProcess ffff8107c6790bf0, ActiveFence 0x142.
  - Serial log: no m1n1/AGX fault line.
- Winsys maps imported presentation BOs with AGX_GPUVA_MAP_WRITE (agx_win32_asahi_bo.c:194-196).

## Candidates (not yet discriminated)

1. GPU write to the presentation allocation faults (UAT PTE attributes for non-class local allocations,
   e.g. write/cacheability derived from the DXGK_PTE of a presentation allocation) → firmware stalls.
2. Previously refused work (shader uploads, private scenes) now executes; one job's content hangs
   (bad shader/state) independent of the render target.
3. Presentation surface geometry/pitch vs attachment layout (ImportLinearColor32) mismatch.

## Next step (offline first)

- Decode the firmware event ring / queue cursors for fence 0x142 exactly as R152 did (Timeout vs fault
  events, TA/3D read/complete cursors), and read the UAT leaf PTEs for VA 0x1060000 in process
  ffff8107c6790bf0 (write/attribute bits) vs a canonical class BO.
- If a UAT write fault: fix PTE attribute derivation for non-class local allocations.
- If a pure timeout with valid mappings: bisect R158 vs R159 in hardware (single variable each).
