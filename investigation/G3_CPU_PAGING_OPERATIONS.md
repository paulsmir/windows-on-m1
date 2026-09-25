# G3 16-KiB CPU paging operation contract (R80/R81)

Sources: pinned WDK 26100 `d3dkmddi.h` lines 4593–4628 and operation
structures in that header; Microsoft Learn `DXGK_BUILDPAGINGBUFFER_OPERATION`
and system paging process; local `paging_windows.c`,
`gpuva_g3_paging_windows.c`, `allocation_windows.c`, and G3 caps in
`lifecycle.c`/`callbacks.c`. Hardware evidence: EXP796 and EXP797.
“Possible” means the advertised path may request it; it does not claim a
hardware observation. Unknown requests fail closed and become the next trace.

| ID | WDK operation | G3 16-KiB expectation and source | Current owner |
|---:|---|---|---|
| 0 | TRANSFER | Legacy WDDMv1; possible for segment migration (`d3dkmddi.h`); physical MDL path | `paging_windows.c` CPU plan |
| 1 | FILL | Legacy WDDMv1; possible for segment clear (`d3dkmddi.h`); physical path | `paging_windows.c` CPU plan |
| 2 | DISCARD_CONTENT | Legacy WDDMv1; possible on eviction (`d3dkmddi.h`) | `paging_windows.c` CPU plan |
| 3 | READ_PHYSICAL | WDDMv1 only; G3 WDDM3 path does not advertise it | fail closed |
| 4 | WRITE_PHYSICAL | WDDMv1 only; same | fail closed |
| 5 | MAP_APERTURE_SEGMENT | Possible: segment 1 is aperture; WDK common WDDMv1/v2 | `paging_windows.c` CPU aperture map |
| 6 | UNMAP_APERTURE_SEGMENT | Possible for that aperture mapping; WDK common | `paging_windows.c` CPU aperture unmap |
| 7 | SPECIAL_LOCK_TRANSFER | WDDMv1 only; G3 WDDM3 path | fail closed |
| 8 | VIRTUAL_TRANSFER | Possible with GpuMmu; local↔system replayed | G3 Build + CPU worker |
| 9 | VIRTUAL_FILL | Observed EXP797 `Wom1G3WorkInput` | G3 Build + CPU worker |
| 10 | INIT_CONTEXT_RESOURCE | Conditional on context resource allocation; none created in current KMD | fail closed |
| 11 | UPDATE_PAGE_TABLE | Observed EXP793/794 | G3 R70 shadow/broker path |
| 12 | FLUSH_TLB | Observed EXP796 | G3 R79 path |
| 13 | UPDATE_CONTEXT_ALLOCATION | Conditional on context allocation; none created in current KMD | fail closed |
| 14 | COPY_PAGE_TABLE_ENTRIES | Conditional on tiled resources; no tiled resource advertisement | fail closed |
| 15 | NOTIFY_RESIDENCY | Conditional on explicit residency notification; allocation flags leave it clear | fail closed |
| 16 | SIGNAL_MONITORED_FENCE | Possible with monitored fence; host replayed | G3 Build + CPU worker |
| 17 | MAP_APERTURE_SEGMENT2 | Requires MapAperture2Supported, not advertised | fail closed |
| 18 | NOTIFY_FENCE_RESIDENCY | Requires native fence path, not advertised | fail closed |
| 19 | MAP_MMU | IOMMU path; IoMmuSupported clear | fail closed |
| 20 | UNMAP_MMU | IOMMU path; IoMmuSupported clear | fail closed |
| 21 | NOTIFY_RESIDENCY2 | WDDM 3.2 residency extension not advertised | fail closed |
| 22 | NOTIFY_ALLOC | Conditional on eviction/IOMMU notification flags, clear here | fail closed |

Build encodes paging records in VidMm's DMA/private buffers. The paging
system context's SubmitCommandVirtual validates those records and copies them
into the existing ordered CPU queue; its worker resolves G3's logical IPA and
aperture/MDL-backed bytes, executes them, then reports the fence. EXP797 proved
Build emitted a 34-record first packet (DMA `0x220`, private `0xff0`) and that
the old SubmitCommandVirtual rejected it. EXP798 tests that exact boundary.
