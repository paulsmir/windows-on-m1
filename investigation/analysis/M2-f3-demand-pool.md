# M2-f3: segment 3 backed on demand

Date: 2026-10-14. Follows M2-f2 (window, EXP1250-EXP1253) and M2-f2c (reserve v3, EXP1254/EXP1255).
EXP1256 rejected sizing a pinned pool from Windows RAM: a pinned pool is memory Windows loses whether
or not the GPU uses it. This step makes segment 3 cost only what VidMm actually places in it.

## Sources inspected

- Microsoft (raw copies in the session scratchpad `docs/`, `ddi/`, CC-BY-4.0; pinned WDK 26100
  `d3dkmddi.h`):
  - `calculating-graphics-memory`: DedicatedSystemMemory = sum of PopulatedFromSystemMemory
    segments, at most TotalSystemMemoryAvailableForGraphics
    (`MIN(80%, MAX(RAM - 16 GB, 50%))`). MaxSharedSystemMemory = that minus DedicatedSystemMemory.
  - `_dxgk_segmentdescriptor*`: a memory segment's commit limit is its size.
  - `initializing-use-of-memory-segments`: segments are queried at initialization only. There is no
    resize, no hot-add and no callback that changes a segment.
  - `gpu-segments`: content enters and leaves memory segments only through Fill/Transfer (Virtual)
    paging operations. A CPU-visible segment is CPU-mapped "directly" at its CpuTranslatedAddress.
  - `examples` (MakeResident): an allocation resident in a local memory segment is zero-filled with
    FillVirtual.
  - `ne-d3dkmddi-_dxgk_buildpagingbuffer_operation` with the header: a WDDM 2 GpuMmu driver gets no
    DISCARD_CONTENT. NOTIFY_RESIDENCY needs AccessedPhysically and ExplicitResidencyNotification;
    NotifyEviction (WDDM 3.2) covers aperture and system segments only.
  - `dxgkddi_destroyallocation`, `allocation-usage-tracking`: destruction waits for queued GPU work.
    No DDI reports that a memory-segment range became free.
  - `dxgkddi_buildpagingbuffer`, `dxgkddi_destroyallocation`: PASSIVE_LEVEL.
- This driver: `pool_windows.c` (fixed 1 GiB pool), `gpuva_g3_paging_windows.c` (UpdatePageTable
  resolves segment-3 PTEs to window IPAs at build time; the paging process has CPU-only logical
  PTEs; virtual paging records carry resolved IPAs), `paging_windows.c` (one worker batch per
  paging fence; EXP1026: BuildPagingBuffer may return only SUCCESS, ALLOCATION_BUSY or
  INSUFFICIENT_DMA_BUFFER), `allocation_windows.c` (every allocation has Alignment 0x10000 and a
  64 KiB-aligned size; the WDDM 3.2 union reading of that field is unresolved, R104).
- m1n1: `hv_agx_pool_window.c` (23d30a32: MAP_AT/UNMAP_RANGE, UNMAP refused while the GPU broker
  holds a backing). `hv_vm.c` (`hv_window_*`). `hv_handle_dabort`: a guest access to an unmapped
  window IPA prints `HV: Unmapped IPA 0xd...` and stops the guest in the proxy.
- Earlier decisions kept: VidMm system memory cannot back 16 KiB UAT pages (EXP1244,
  `M2-vidmm-64k-decision.md`). Segment 3 stays a KMD-populated memory segment.

## Contract

VidMm owns placement inside segment 3. It never tells the KMD that a range was allocated or freed.
The KMD learns of a placement only when VidMm references it:
- an UpdatePageTable PTE: allocation handle, segment offset, allocation offset;
- a virtual paging record, through the paging process's PTEs.

Every access to a segment-3 page goes through one of three paths:
- **GPU**, through a UAT leaf published from a PTE. m1n1 refuses to unmap a page while the broker
  holds its backing.
- **CPU paging executor**, through a record's IPA. The record was resolved at build time from a
  paging-process PTE.
- **CPU**, through the segment's CPU mapping. This covers both VidMm Lock mappings and the KMD's
  own write-combined view.

## Ownership

| Concern | Owner |
|---|---|
| Segment-3 placement and content lifetime | VidMm |
| Window page commit and release (Windows pages, MAP_AT/UNMAP_RANGE) | KMD pool (`pool_windows.c`) |
| Stage-2 window entries, the no-unmap-while-GPU-backed rule | m1n1 |

## Granule state machine (portable, `render_pool_demand.c`)

Granularity is 64 KiB: the Windows allocation unit (`MmAllocatePagesForMdlEx` chunk, 64 KiB
aligned) and the allocation alignment. Each granule has:
- `Committed`;
- `Owner`: the allocation whose current placement covers it;
- `PagingRefs`: paging-process PTEs that point into it;
- `RecordSeq`: the last paging record that names it;
- `Pinned`.

- **Place(A, base, bytes)**, on a valid segment-3 PTE for A in any process:
  - If A was placed elsewhere, A's old granules lose their owner.
  - Any other allocation that owns a granule in A's new range is no longer resident there (VidMm
    never overlaps two resident allocations), so its whole placement loses its owner.
  - Then every granule in A's range is committed and owned by A.
  - Commit happens at build time, before the PTE is published or the paging record is encoded.
- **Unplace(A)** happens:
  - in DestroyAllocation;
  - when a user process (not the paging process) maps A validly in another segment. The paging
    process maps eviction destinations before VidMm decides anything about the source.
- **Pin**: placements that break the 64 KiB contract (unaligned base, no allocation handle) are
  committed and never released. They are counted.
- **Release** happens only when all of the following hold. These are checked in the paging
  worker after each batch and in DestroyAllocation:
  - the granule has no owner, no paging refs and is not pinned;
  - every record that named it has executed;
  - UNMAP_RANGE succeeds (no UAT backing in any process).
  A granule that is still busy is retried after the next batch.

None of these release conditions depend on the order in which VidMm builds a move. A record that
reads the old range holds that range through `RecordSeq`. The paging-process mapping holds it
through `PagingRefs`. User-process UAT leaves hold it through m1n1.

## Differences from the fixed pool

| Aspect | Fixed pool (EXP1253-EXP1255) | Demand pool |
|---|---|---|
| Pages at StartDevice | 1 GiB pinned | none |
| CPU view | `MmMapLockedPagesSpecifyCache` of the MDL | `MmMapIoSpaceEx` of the window IPA, write-combined |
| Release | UNMAP_ALL at stop | per granule at run time |

In both, the segment size stays 1 GiB in this step. Larger segment 3 and budget groups are later,
separate variables.

## Known gap (hypothesis, measured by the checkpoint)

A CPU Lock of a segment-3 allocation that no PTE has ever referenced would touch an uncommitted
page. The docs say the zero-fill (FillVirtual through the paging process) precedes any use of a new
allocation. No Microsoft page says whether a Lock waits for it, or whether a not-zeroed allocation
skips it. A violation is unambiguous: m1n1 logs `Unmapped IPA 0xd...`.

## Smallest falsifiable hardware checkpoint

One variable: demand commit instead of the pinned 1 GiB, with the same segment size and placement.

| Phase | Expected |
|---|---|
| Idle desktop | Committed bytes well under 1 GiB; Windows available memory above EXP1255's median |
| CS benchmark | Passes; peak commit is close to the segment-3 peak (~670 MiB, EXP1256) |
| After the game exits | Committed bytes fall back |
| Throughout | No `Unmapped IPA`, no window refusals other than Busy retries, no commit failures |

Recovery is the existing package-absent rollback.
