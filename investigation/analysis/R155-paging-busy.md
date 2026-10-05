# R155 — EXP868 0x10E/0xB: G3 UpdatePageTable returns STATUS_DEVICE_BUSY

Offline analysis (operator Claude; Codex weekly limit exhausted). No code change yet.

## Evidence (EXP868, package 868 = e210d8be, R154 mapping revalidation)

- 600 s checkpoint passed (613.46 s Code0, DWM/Explorer stable). After the checkpoint a
  spontaneous reset; minidump `092826-15140-01.dmp`: bugcheck `0x10E`, p1 `0xB`,
  p2 `0xffff868eb7850208`, p3 `0xffffffff80000011` (STATUS_DEVICE_BUSY), p4 `0xffffa60149cd0000`.
- Persisted receipts (hidden boot `state.json`): `Wom1G3PagingStatus = 0x80000011`;
  `Wom1G3PagingInput` (v2, written only for UPDATE_PAGE_TABLE): Operation 11
  (`DXGK_OPERATION_UPDATE_PAGE_TABLE`), UpdateMode 2 (`DXGK_PAGETABLEUPDATE_GPU_PHYSICAL`),
  Level 0, DMA and private data present.

## Source

`drivers/apple-agx/render-admission/src/gpuva_g3_paging_windows.c`, `AdmissionG3UpdateLeaf`:

```c
if (process->Graph.JobInFlight || process->Graph.LeaseToken)
  return STATUS_DEVICE_BUSY;
```

Introduced by R132 `f53f4f98` (residency-scoped system-frame lifetime). The shared graph also
refuses mutation while `JobInFlight` (`AppleAgxGpuvaG3GraphUpdateParent`). Before R154 the
worker vetoed any job whose graph generation changed, so jobs rarely overlapped a VidMm page-table
update. R154 (599fca20) correctly lets such jobs run; VidMm now issues UPDATE_PAGE_TABLE for the same
process while a native job is in flight, and the guard returns an illegal status.

## WINDOWS CONTRACT

- `DxgkDdiBuildPagingBuffer` may return only STATUS_SUCCESS,
  STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER, or STATUS_GRAPHICS_ALLOCATION_BUSY (documented for
  Transfer / DiscardContent / SpecialLockTransfer only). Any other status is a critical failure
  (VidMm bugchecks) — matches 0x10E.
- UPDATE_PAGE_TABLE with `UpdateMode = GPU_PHYSICAL` describes a page-table write that the driver
  encodes into the paging buffer; it takes effect when the paging buffer executes, in paging-queue
  order. Only the paging-process initialization case (CPU_VIRTUAL, `pDmaBuffer == NULL`) requires an
  immediate update. VidMm orders paging against render work; it does not promise the process is idle.

## AGX CONTRACT

Native UAT tables are live while a job runs. Changing entries the running job does not reference is
hardware-safe (followed by the FLUSH_TLB paging op). Changing a 16 KiB leaf group containing a page
the running job references would fault it; VidMm does not unmap in-use allocations, but
4 KiB/16 KiB group sharing remains the known representability risk (R133/R142).

## TRANSLATION (proposed)

Stop applying GPU_PHYSICAL updates at build time. Encode each G3 UPDATE_PAGE_TABLE (copy of the PTE
range, process, level, table address, generation) as a paging record, return STATUS_SUCCESS (or
INSUFFICIENT_DMA_BUFFER when space is short), and apply it in the paging execution worker, in queue
order, before signalling the paging fence. If the target process has a native job in flight at
execution time, the worker waits for that job's joined completion (it is our own worker, not the
VidMm caller) and then applies; the paging fence therefore only completes after the table change,
which is exactly the GPU_PHYSICAL semantic. Prerequisite check: the paging worker must not be the
same serialized worker that retires native jobs (otherwise the wait deadlocks) — verify in
`work_queue_windows.c` / `interrupt.c` before implementing.

Rejected alternatives: returning STATUS_GRAPHICS_ALLOCATION_BUSY (not documented for
UpdatePageTable); blocking inside BuildPagingBuffer (blocks VidMm's paging thread; completion
ordering not guaranteed); applying immediately during a job (breaks R132 system-frame lifetime and
the graph's no-mutation-during-job invariant).

## WHAT IS STILL UNKNOWN

1. Whether the paging execution worker is independent from native job retirement (source check).
2. Whether VidMm ever issues a GPU_PHYSICAL update that touches pages referenced by the in-flight
   job (hardware-only; the wait-at-execution design is safe either way).

## Other EXP868 observations

- UMD (log capped per load): create-device 1205, draw-before 595, present 2, reject 2151,
  HRESULT failures 13541. Not analysed yet; no scanout frame evidence.
- Live collector could not stop the EXP801DxgBoot ETL session (guard tripped); six periodic
  original-boot snapshots exist in `.local/experiments/EXP868-r154-mapgen/periodic-original-boot/`.

## Proposed EXP869 single variable

Deferred G3 UPDATE_PAGE_TABLE application at paging execution (after the prerequisite check and a
RED→GREEN replay: UpdateLeaf during an in-flight job must not return a non-WDDM status and must apply
after joined completion, before the paging fence signal).
