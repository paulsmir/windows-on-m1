# M2 (UMA) fact base

Date: 2026-10-12. Read-only survey for roadmap step M2 (UMA: the GPU allocates
from system memory through GpuMmu, the fixed 1 GiB local reserve shrinks to
firmware + scanout needs) and M1 (ramdisk window). Paths are relative to the
repository; the pinned WDK is `.local/reference/wdk26100`. No design choice is
made here.

## 1. Current memory model in the KMD

**Shipped build profile.** `-GpuvaG1bPageProfile 16` (`scripts/g3-verified-build.ps1:22`): WDDM 3.2,
local SlabSize 16 KiB, local `Use64KBPages` 0 (`include/gpuva_g1b_profile.h:15-34`).
`ADMISSION_GPUVA_G1B_ALLOCATION_HINT` defaults to 0. The G3 path builds only with profile 16 or 64
(`src/lifecycle.c:7-10`).

**Reserve layout** (`src/memory_runtime_windows.c:4-14`): local GPU VA 0x1500000000, 1 GiB =
952 MiB VidMm (`LOCAL_ALLOCATION_BYTES` 0x3b800000) + 64 MiB KMD private scenes + 8 MiB backend
(render-template inputs, shader aliases) at 0x3f800000. Aperture: GPU VA 0x1600000000, 256 MiB,
software aperture of 65536 4-KiB entries.

**How the reserve reaches the device.**
1. m1n1 picks align64K(`low_mem_pa + low_mem_size`) = 0x8E0000000, validates every 4 KiB as
   identity IPA = PA (`hv_agx_local_reserve.c:60-75`), publishes a read-only receipt at broker
   offset 0xD00 (`:150-215`).
2. Mu re-validates it and reserves it (`EfiReservedMemoryType` HOB, `MemoryInitPeiLib.c:438-489`).
3. ACPI `_CRS` exposes it as the 5th QWordMemory resource (`J313AppleAgxAbiAdmission.asl.inc`).
4. The KMD borrows it (`src/physical_memory_windows.c:328-458`): one CM resource, `MmMapIoSpaceEx`
   write-combined, HVC IPA->PA check, zeroed, mapped in UAT context 63 at 0x1500000000, split
   952/64/8 MiB. Any other size is refused (`lifecycle.c:120-124`; ABI
   `shared/include/apple_agx_local_reserve_abi.h`, `GuestIpa == HostPa`).

**Segments** (`shared/src/apple_agx_physical_topology.c:33-48`; ids aperture 1, local 2):

| Segment | Source | Reported |
|---|---|---|
| 1 aperture | `memory_windows.c:22-38` | SYSMEM, Aperture 1, CpuVisible 0, PopulatedFromSystemMemory 0, Use64KBPages 0, 256 MiB, SlabSize 4 KB |
| 2 local | `memory_windows.c:74-90` | LOCAL, CpuVisible 1, PopulatedByReservedDDRByFirmware 1, Use64KBPages 0, 952 MiB, CpuTranslatedAddress = reserve IPA, SlabSize 16 KB |

The paging buffer lives in segment 1 (4 KiB).

**DXGK_GPUMMUCAPS** (`lifecycle.c:728-768`, `shared/include/apple_agx_gpuva_g3_caps.h`):
ReadOnlyMemorySupported 1, SysMem64KBPageSupported 1 (R134), ExplicitPageTableInvalidation 1,
PageTableUpdateRequireAddressSpaceIdle 0 (EXP986), PageTableUpdateMode GPU_PHYSICAL,
VirtualAddressBitCount 39, PageTableLevelCount 3, LeafPageTableSizeFor64KPagesInBytes 0x4000
(native AGX leaf 16 KiB, R59/EXP781); everything else 0.

**Page table levels** (`lifecycle.c:770-808`): leaf 13 index bits, 0x20000 bytes (8192 x 16 B,
32 MiB of VA); level 1 11 bits, 0x8000; root 3 bits, 0x4000. All in segment 2, alignment 0x4000.
AGX consumes each aligned group of four 4-KiB logical leaf entries as one native 16-KiB PTE.

**CreateAllocation placement** (`src/allocation_windows.c:438-487`): Alignment 0x10000, preferred
segment 2, EvictionSegmentSet 0, AccessedPhysically 1. Not CPU-visible: local only (0x2).
CPU-visible: 0x3, local preferred (EXP836 admitted shape; local-only refused, EXP1027).
CPU-visible class0 (CDD/GDI section-backed): aperture only (EXP837-840 Rotate AV). Primaries: local
only. In WDDM 3.2 `Alignment` overlaps `MinimumPageSize`/`RecommendedPageSize` (0x10000 reads as
Min 0 = 4 KB, Rec 1 = 8 KB; REVIEW R104, unresolved).

**Paging path to the UAT** (`src/gpuva_g3_paging_windows.c`):
- Use64KBPages accepted only when Leaf64KBytes != 0 (true in profile 16); root level refused;
  GPU_PHYSICAL or CPU_VIRTUAL only (`:1220-1239`).
- Tables must be in the local view (`:1291-1302`); parent PTEs point to segment 2 (`:238-243`).
- Broker tables are shadows in ordinary system RAM (16 KiB contiguous physical memory objects,
  `src/gpuva_g3_windows.c:247-300`): the UAT already walks tables in Windows RAM (hardware-proven).
- Leaf update (`:334-541`): for segment != local, `PageAddress << 12` is the guest IPA. A 16 KiB
  group is published only when all four logical PTEs are valid, same flags and segment,
  IPA-contiguous and 16K-aligned, and the segment is 0 or 2-inside-local. Complete segment-0
  groups: REGISTER_SHARED_BACKING then UPDATE_LEAF. Broker OWNERSHIP/CAPACITY refusals leave the
  leaf invalid (R133). Incomplete groups count in `UnpublishedGroups[segment]` and return SUCCESS.
  VidMm emits system pages as segment 0 (EXP965: [0]=149376, [1]=0).
- 64K updates expand into 16 logical entries and invalidate `LogicalPtes`, so the CPU paging
  executor cannot reach 64K-mapped VAs.
- The system paging process is CPU-only (EXP1122). Only SUCCESS / ALLOCATION_BUSY /
  INSUFFICIENT_DMA_BUFFER may be returned (`:1016-1054`).
- The G4 parser rejects unpublished declared ranges but probes render addresses for one byte only:
  shader-accessed buffers are not checked (`investigation/analysis/EXP850-sysmem-translation.md`).

## 2. History

REVIEW (`.local/tandem/REVIEW.md`): R70 DONE (publish only complete groups; a single 4 KiB system
page cannot be published without exposing foreign memory), R71 OPEN (VidMm can be made to give
64 KiB contiguous system chunks; EXP772's "16 KiB rejected" was confounded), R72 profile 16 primary,
R76 scattered groups not published, R86 OPEN (range grants needed), R68 shadow tables, R59 Leaf64K,
R56 tables CPU-visible, R104 alignment union.

| Experiment | Ledger line | Conclusion |
|---|---|---|
| EXP772 | 47479/47489 | 16 KiB local + GpuMmu refused, confounded by the PASID veto (EXP776) |
| EXP773 | 47506 | 64 KiB failed the same way |
| EXP790/791 | 47812-47841 | profile-16 failure was a KMD self-veto; 16 KiB with GpuMmu admitted |
| EXP836 | 48870 | local-only write set refused; 0x3 passes |
| EXP837-839 | 48879-48888 | Rotate AV on CPU lock of a section-backed class0 surface in segment 2 |
| EXP840 | 48893 | class0 CPU-visible surfaces aperture-only |
| EXP852 | 48977 | leaf failure on a firmware-region PFN reclaimed by Windows |
| EXP853 | 48979-48984 | 338909 unpublished segment-0 groups; scattered misaligned IPAs; PARTIAL |
| EXP854 | 48986-48993 | SysMem64KB declared; 9474 unpublished; no evidence of 64K updates; INCONCLUSIVE |
| EXP1004 | 53119 | no Use64KBPages leaf updates in any ring |
| EXP1025 | 53189 | UMD SystemDirect -> 0x10E (paging status-contract defect) |
| EXP1026 | 53192 | system placement -> hang -> 0x116 (TDR reset C0000483); cause a labelled hypothesis; REJECTED system placement |
| EXP1027/1028 | 53196-53200 | local-only CPU-visible class allocations refused; 0x3 local-preferred stable (~220/952 MB used) |

Analysis: `EXP853-granule-decision.md` (options SysMem64KB / larger reserve / staging; union trap),
`EXP852-broker-range-design.md`, `R143-reserve-1g-design.md:197-232` (v5 capacity, v6 range grants).

## 3. Pinned WDK fields

`d3dkmddi.h`: DXGK_PAGE_TABLE_LEVEL_DESC (2072-2079); DXGK_UPDATEPAGETABLEFLAGS (Repeat,
InitialUpdate, NotifyEviction, Use64KBPages, NativeFence); DXGK_GPUMMUCAPS (2113-2161, incl.
SysMem64KBPageSupported, LeafPageTableSizeFor64KPagesInBytes; no OpportunisticSysMem64KB or
PerPtePageSize in this header); DXGK_SEGMENTFLAGS (2559-2603: PopulatedFromSystemMemory,
Use64KBPages, PopulatedByReservedDDRByFirmware ...); DXGK_SEGMENT_ID_SYSTEMMEMORY 31;
DXGK_SEGMENTDESCRIPTOR4 (CommitLimit, aperture only) and DESCRIPTOR5 (SegmentType, Size,
CpuTranslatedAddress, SlabSize; no CommitLimit); DXGK_ALLOCATIONINFOFLAGS_WDDM2_0 (CpuVisible,
PermanentSysMem, Cached, ExistingSysMem, DisableLargePageMapping, MapApertureCpuVisible,
AccessedPhysically, ExplicitResidencyNotification); DXGK_ALLOCATIONINFOFLAGS2 (WDDM 3.2:
DisablePartialResidency, RestrictedToSingleSegment, NotifyEviction); DXGK_ALLOCATIONINFO
(Alignment / MinimumPageSize+RecommendedPageSize union); DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE
(4693-4708). `d3dukmdt.h`: DXGK_PTE (315-341; PageAddress is a page number).

## 4. m1n1

- Stage 2 has a 16 KiB granule; guest RAM is identity-mapped; the low window IPA
  0x100000-0x40000000 is backed at 0x8a0100000 (`hv_vm.c`).
- The GPU sees host PA and can reach any guest RAM page that passes `translate_guest`
  (`hv_agx_retained_platform.c:48-71`): 16K-aligned IPA inside guest RAM, not the retained root, not
  heap/firmware(FD)/ADT/bootargs/framebuffer/DART tables, not the ramdisk window, stage-2
  PA-contiguous. Reachability is not limited to the local window.
- Broker v5 (`hv_agx_gpuva_v5.c`): 128 processes, 512 tables, 8192 backings (system pages only
  entries 0-4095: at most 64 MiB of unique 16K system pages), 64 slots, 65536 local pages;
  `configure_local` requires 64 MiB private; `update_leaf` all-or-nothing on four sub-pages;
  revoke scans every leaf table. UAT mappings are shared inner non-cacheable.
- Scanout pool = reserve minus 24 MiB, identity only; 0x3b800000 duplicated in
  `hv_agx_scanout_broker.h:31` and `shared/include/apple_agx_scanout.h:39`.
- Broker cost: 188 s in 20 min (EXP1085); about 4 us per call after 96a0c1ef (EXP1127).

## 5. M1: the ramdisk window

Defined in `config/j313-guest-layout.json` -> `guest_layout.py` -> `J313GuestLayout.dsc.inc`
(`PcdPreloadedRamdiskBase` 0x860000000, MaxSize 1 GiB) and `hv_autonomous_layout.generated.h`.
Mu always reserves it (`MemoryInitPeiLib.c:398-412`); only `BootRamdiskHelperDxe.c:65-96` checks for
the "ASIRAMDK" header. Only the assisted path loads a ramdisk (`run_uefi.py:305-325`). m1n1
excludes the window from AGX backings regardless (`hv_agx_retained_platform.c:62`,
`hv_agx_power_mmio.c:72`); autonomous launch needs kernel data to end at or below `ramdisk_base`
(`hv_autonomous_runtime.c:228`).

## Constraints for any M2 design

1. Granule: only complete 16 KiB groups publish; VidMm system backing is 4 KiB and scattered
   unless 64K PTEs are issued; unpublished shader accesses are not caught; TDR reset fails (0x116).
2. Broker reachability: normal guest RAM except FD/firmware, m1n1 heap/ADT/bootargs/framebuffer,
   the ramdisk window, table and mailbox pages; the local reserve must stay identity-mapped.
3. Broker capacity and cost: 4096 system backings (64 MiB), 512 tables, fixed 64 MiB private,
   per-page synchronous hypercalls.
4. VidMm rules: write set 0x3 for CPU-visible class allocations; section-backed class0 aperture
   only; primaries local only; SYSMEM segment Size is its limit; tables in segment 2 and
   CPU-visible; three BuildPagingBuffer statuses; 64K PTEs break CPU paging through LogicalPtes.
5. The 1 GiB / 952 MiB constants are spread across m1n1, Mu, the ACPI generator and the KMD.
6. CPU coherence: UAT is non-cacheable and the KMD never sets Cached: system memory the GPU
   writes must be WC or uncached on the CPU side.

## Candidate designs (evidence only)

- A. SysMem64KB (declared since R134; no 64K updates observed: EXP854, EXP1004).
- B. Driver-allocated, PopulatedFromSystemMemory segment carved at StartDevice (EXP804/805 ran CDD;
  a 64 MiB contiguous allocation failed at live start, R63/R69).
- C. Larger or smaller local reserve with explicit staging (EXP853 options B and C).
- D. Broker range grants (v6; R86, R143) -- infrastructure any UMA design needs.
- E. Post-ExitBootServices release of the FD region (EXP852 range design).
- F. VidMm page tables outside local (shadow tables already live in system memory; conflicts with
  the KMD's local-view checks and R56).
- G. Conditional ramdisk exclusion in m1n1 together with M1.

## Open questions for hardware

- Does VidMm ever issue segment-0 Use64KBPages updates, and for which allocations? Effect of
  AccessedPhysically, Min/RecommendedPageSize, a SYSMEM SlabSize of 16K/64K?
- Complete-group rate of aperture-resident GPU allocations, joined to allocation identity.
- The real cause of EXP1026 (rerun with receipts kept and UAT fault capture).
- The GPU working-set high-water mark and the minimum firmware + scanout + tables + private size
  (three 2560x1600 primaries are 46.9 MiB).
- Broker behaviour at UMA scale (CAPACITY hits, ms/s of broker time).
- Whether dxgkrnl accepts a larger aperture; residency under local pressure.
- M1: can a stale "ASIRAMDK" header survive a reset?
