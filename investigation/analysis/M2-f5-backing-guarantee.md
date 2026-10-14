# M2-f5: segment-3 backing under memory pressure (2026-10-14)

M2-f3 made segment 3 cost only what VidMm places there. Its pages come from Windows at the moment
of placement. This step decides where an out-of-memory condition is reported, so that it reaches the
caller the WDDM contract names.

## Sources inspected

- Microsoft (pinned WDK 26100, raw copies in the session scratchpad):
  - `dxgkddi_createallocation`: STATUS_NO_MEMORY when the call "could not allocate memory that was
    required for it to complete". The runtime reports E_OUTOFMEMORY to the application.
  - `dxgkddi_buildpagingbuffer`: only SUCCESS, ALLOCATION_BUSY or INSUFFICIENT_DMA_BUFFER may be
    returned. EXP1014/EXP1025/EXP1026: anything else bugchecks 0x10E/0xB.
  - `_dxgk_segmentdescriptor*`: a memory segment's commit limit is its size.
  - `initializing-use-of-memory-segments`: segments are fixed at initialization.
- This driver:
  - `gpuva_g3_paging_windows.c`. `AdmissionG3PoolPlace` commits the placement inside UpdatePageTable;
    a failed commit is STATUS_INSUFFICIENT_RESOURCES. `AdmissionGpuvaG3BuildPagingBufferChecked`
    turns that into SUCCESS and poisons the owning process. A poisoned process's later paging and
    GPU work is refused.
  - `allocation_windows.c`. Segment 3 holds the CPU-visible Mesa class allocations (with the aperture
    as fallback). Each CreateAllocation creates exactly one allocation.
  - `render_pool_slab.c`. Pages come in 2 MiB slabs, and empty slabs beyond one spare go back to
    Windows.
- m1n1 is unchanged. The broker and window are not involved in this decision.

## Observed contract (offline)

- A placement whose pages Windows cannot supply poisons the process that owns the page table.
- For a user process that is a lost device, reported at an unrelated later call.
- For the paging process, which maps the destination of every restore into segment 3, it refuses all
  further paging. That is a system-wide failure.
- No hardware run has exercised this path yet: EXP1257-EXP1264 reported zero NewSlabFailures.

## Translation

| Concern | Windows contract | This driver |
|---|---|---|
| Report "no memory" | CreateAllocation, STATUS_NO_MEMORY | Reserve the allocation's 64 KiB granules (rounded up to 2 MiB slabs) when it is created; refuse with STATUS_NO_MEMORY |
| Placement (UpdatePageTable, any process) | must not fail for memory | Takes reserved slots; Windows is not asked |
| Commit limit | the segment size | Reservations count up to the segment's granules. Beyond that VidMm cannot place more, so no pages are needed |
| Release | allocation destruction | DestroyAllocation ends the reservation; slabs return to Windows only while the rest still covers what is reserved |

What remains in the paging path is the brief overlap of a move inside segment 3: the old range is held
until its records execute while the new one is placed. That case still asks Windows for pages beyond
the reservation, with the old behaviour on failure. It is counted (NewSlabFailures) and expected to
be rare.

The cost: the driver now holds pages for every live segment-3 allocation, not only for the resident
ones. These differ only while allocations are evicted or not yet placed. The checkpoint measures the
difference (receipt v6 ReservedBytes against committed).

## Hardware checkpoint (EXP1265, one variable: reservation at creation)

| Phase | Expected |
|---|---|
| Idle, CS benchmark | fps as EXP1264 (46.17); ReservedBytes close to committed; memory returns after the game |
| `agx_mempressure.exe` (lock RAM to 384 MiB available, then 32 MiB render targets until failure) | creation fails with E_OUTOFMEMORY (0x8007000E), the device is not removed, readback works; ReserveFailures >= 1; no poisoned process; DWM and SSH alive |

Recovery: the existing package-absent rollback. The pressure tool frees its locked memory itself.

## Next gap (M2-f6, not in this step)

The m1n1 GPU broker records at most HV_AGX_GPUVA_V5_BACKINGS = 131072 system pages of 16 KiB
(2 GiB) in a static table of about 48 bytes per record. The KMD therefore caps segment 3 at
1792 MiB on any machine. On J313 the RAM formula alone would give about 3.75 GiB. Sizing the
broker table from the guest's RAM, and reporting the capacity to the KMD instead of the constant
ADMISSION_POOL_DEMAND_BROKER_BYTES, would let segment 3 scale with the Mac.
