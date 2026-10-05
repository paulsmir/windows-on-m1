# G1b: pinned WDK 26100 WDDM 3.2 inventory and discriminator

Status: offline inventory plus a pinned-WDK 26100 **physical-mode** 3.2
diagnostic profile. The profile selects 16 or 64 KiB in
`include/gpuva_g1b_profile.h`; both profiles and the unchanged 3.0 profile
compiled with incremental ARM64 MSBuild. GpuMmu caps remain zero and its
virtual DDIs remain fail-closed. No package has been installed or launched.
The physical-mode profile cannot by itself satisfy the authorized B2 GPUVA
hardware gate; B1 and then G3's coherent GpuMmu contract precede that run.
EXP758 accepted the physical WDDM3.2 adapter with the 64-KiB slab, but its
standard client regressed at CreateSwapChain before graphics work. The 16-KiB
slab run is held while the first failing DXGI/UMD/KMD boundary is attributed
with the observation-only receipts in
`investigation/GPUVA_G1B_SWAPCHAIN_BOUNDARY.md`.
Scope is **FULL GRAPHICS**. Pinned source is builder
`C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared\d3dkmddi.h`,
SHA256 `c13cecb0ce73e7bbdb6bec8586d05eea31932a8c532bec49b3dae4a03054770e`.
The callback-table authority is also pinned `km/dispmprt.h` 2690-3043:
WDDM 3.1 adds ten pointer slots and 3.2 adds 21. The ARM64 initialization
layout grows from 1296 bytes at 3.0 to 1544 bytes at 3.2; the candidate pins
both sizes with compile-time assertions. Newly exposed callbacks cover native
fences, doorbells, dirty tracking, live migration, debug info, context priority
and display reset; the physical-mode trial keeps unsupported feature callbacks
null and their caps clear.
Asahi `pgtable.rs` provides 16-KiB native pages and 3/11/11/14 VA geometry;
`mmu.rs` provides process VM/slot binding. Microsoft GpuMmu and paging DDIs
remain the Windows contract.

## WDK 3.2 surface to close before advertising `DXGKDDI_WDDMv3_2`

`DXGKDDI_WDDMv3_2 = 0x3200` at line 2402. The version exposes the following
new inputs. Presence in a header is **not** evidence that every feature is
mandatory. The driver must give a truthful answer to every query it receives
and keep optional feature bits clear until companion DDIs exist.
The pinned header calls `MinimumPageSize` and `RecommendedPageSize` **input**
fields in the `Alignment` union. Writing them from CreateAllocation is a
separate, disabled-by-default experimental switch
(`AppleAgxGpuvaG1bAllocationHint`). It is not a documented output contract.
The first 3.2 trial keeps the old 64-KiB allocation alignment; the 16-KiB
slab trial changes only the coupled segment-page description. Enable the hint
only as a separately preregistered discriminator if ETW shows why slab alone
is insufficient.

| Surface | WDK 26100 | Required decision for this 16-KiB trial |
|---|---|---|
| `QUERYSEGMENT5` (44), `DXGK_SEGMENTDESCRIPTOR5.SlabSize` | 1866, 11082-11135 | Implement coherent local segment descriptor with `DXGK_PAGESIZE_16KB` only for experimental profile; fallback must use documented 64-KiB segment choice. Keep existing segment identity and display constraints consistent. |
| `QUERYMMUCOUNT` (45), `QUERYMMUS` (46) | 1867-68, 11137-11181 | Return coherent count/descriptors/display MMU ID for advertised MMUs. Do not conflate one AGX UAT with 64 process slots or advertise an unimplemented MMU. |
| `DXGK_ALLOCATIONINFO.MinimumPageSize/RecommendedPageSize` union with `Alignment`; `DXGK_PAGESIZE_16KB=2` | 3912-25, 11082-88 | Candidate sets both sizes through one profile selector. Because fields alias legacy `Alignment` and Microsoft Learn gives no semantic description, verify actual 26100 runtime interpretation before using any observed value as proof. |
| `DIRTYBITTRACKINGCAPS` (39), `DIRTYBITTRACKINGSEGMENTCAPS` (40) | 1861-62, 10439+ | Report unsupported; no dirty-tracking callbacks or caps. |
| `SCATTER_RESERVE` (41) | 1863 | Report unsupported and keep `ScatterMapReserve=0`. |
| `QUERYPAGINGBUFFERINFO` (42) | 1864 | Audit caller/structure and existing paging-buffer response; do not silently return success. This remains an admission item for a real 3.2 candidate. |
| `QUERYSEGMENTCOUNT` (43) | 1865 | Return count matching `QUERYSEGMENT5` and older segment queries. |
| 3.2 cap bits and allocation flags | 1526-31, 2479-85, 2544-50, 3857-66 | Leave TestContext, live migration, scatter reserve, unsatisfied allocation/primary/display flags clear. `DisablePartialResidency` or `RestrictedToSingleSegment` need separate proof before use; they are not automatic fixes for 16-KiB geometry. |
| New paging operations `MAP_MMU`, `UNMAP_MMU`, `NOTIFY_RESIDENCY2`, `NOTIFY_ALLOC` | 4624-29, 4813+, 5057-62 | Accept only when matching advertised MMU/residency/notification contract; otherwise fail closed with supported status. Basic GpuMmu still needs older UPDATE_PAGE_TABLE/FLUSH_TLB. |
| Feature interface and newer DDIs | 10439-11225, 11469-11498 | Audit `QUERYFEATURESUPPORT`, `QUERYFEATUREINTERFACE`, `COLLECTDBGINFO2`, `NOTIFYCONTEXTPRIORITYCHANGE`, `RESETDISPLAYENGINE`; dirty tracking, live migration and native-fence families are conditional on support. Microsoft's 3.2 feature-handshake documentation says KMD needs `DXGKDDI_FEATURE_INTERFACE` to answer port-driver feature queries; provide truthful negative feature answers. |
| Standard allocation fence-storage branch | 4141-78 | Reject unsupported native-fence request explicitly; no fake allocation. |

This is the explicit `#if WDDM3_2` inventory from the pinned `d3dkmddi.h`
blocks at 1526, 1860, 2479, 2544, 3857, 4141, 4176, 4624, 4813,
5057, 8989, 10439, 11469 and 11489. Full callback-table wiring and the
runtime's required-query order must be checked against the exact build before
any candidate is packaged. In particular, header availability cannot establish
that `QUERYSEGMENT5` or 16-KiB slabs are accepted by VidMm.
The G1 generator assumes VidMm obeys the selected segment-page granularity;
it does not prove this assumption. The future KMD G3 UpdatePageTable path must
reject a noncontiguous or unaligned native 16-KiB leaf with a bounded receipt,
not round, alias or silently map it. ETW in the held discriminator must measure
actual placement and PTE granularity against that assumption.

## One preregistered hardware question (held, not executable)

Candidate ID `GPUVA-G1B-16K-ADMISSION` is reserved for **one** run after G2/G3
implementation, offline ABI tests and explicit user permission. Variable:
select the experimental 16-KiB profile at the single caps selector, with all
other package/firmware/recovery state pinned. `WHY THIS HYPOTHESIS:` (1) pinned
WDK has `DXGK_PAGESIZE_16KB` and segment/allocation fields; (2) Microsoft
documents 16-KiB GPU pages over 4-KiB logical updates; (3) Asahi has 16-KiB
UAT leaves. These make 16-KiB support plausible, not proven.

Success requires **both**: adapter naturally starts as 3.2 full graphics rather
than Basic Display; ETW VidMm plus bounded KMD receipts show 16-KiB placement
and UpdatePageTable groups with no 4-KiB physical scatter/partial protection
contradiction. A start failure, 64/4-KiB placement, unrepresentable update or
missing ETW identity rejects the 16-KiB hypothesis. Then select the documented
64-KiB profile in the same caps location for a separately preregistered run;
do not silently switch during the same experiment.

Before this ID becomes a hardware entry, record actual build/install/launch
commands, commit/diff hashes, package manifest and SHA256, recovery artifact,
ETW paths and failure timeout in `EXPERIMENTS.md`. No artifact exists yet, so
the held question is **not** a launch-ready hardware preregistration. The
operator's explicit permission is required before the run.

## Remaining offline blocker

Microsoft describes VidMm's page-table indices as 4-KiB logical entries,
whereas native AGX's three tables index 16-KiB leaves. The G1 descriptor
projection (3/11/13 logical bits compressed into native 3/11/11, 16-KiB local
tables) is a candidate, not a proven
`PAGETABLELEVELDESC` response. Resolve index/coverage and partial-table update
semantics in the pinned DDI before advertising GpuMmu or running G1b.
