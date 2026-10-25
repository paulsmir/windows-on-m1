# RC13: VidMm page-in capacity and opt-in system-memory placement

Offline work at base 029fb7097b7133a6af050fb95b43eb1455f9ee6a. D below means
`drivers/apple-agx/render-admission`; W means `.local/reference/wdk26100`.
No hardware, guest, builder, package, firmware, or hypervisor changes.

## Cause and evidence

The proven failure boundary is VidMm page-in, followed by device error 0xc
(FailedToPageInRequiredResource), before the runtime's own removal. Case A
(placement exhaustion) is supported; the proposed Case B pool/floor refusal is
excluded by the lifetime counters. This does not prove every resident allocation
was pinned, or identify the exact internal VidMm branch from a generic status.

Evidence root: `.local/experiments/EXP1951-rc11-errors/evidence` in the shared
repository-local area, not this worktree's `.local`.

- `etw/vidmm-pagein-failure-excerpt.csv:164`: GM local commitment 1,370,570,752
  bytes; segment capacity 1,409,286,144 bytes. Even ignoring other clients,
  only 38,715,392 bytes remain. `:166–180`: allocation 0xFFFFE101BA9F6C80,
  iteration 3, UNDER_COMMITTED, PlacementRestrictionNone, TRIM_NONE; three
  failures, unrecoverable page-in, then VidSchErrorFailedToPageInRequiredResource.
- A bounded search of `etw/gm1951-dxg.csv:214344–214345` finds an earlier
  successful system-to-segment-3 transfer of that same allocation: 67,174,400
  bytes. That exceeds the remaining capacity even before other clients.
  The CSV is circular evidence; this is an observed transfer size, not an
  independently reconstructed complete allocation descriptor.
- `{Operation Failed}` matches STATUS_UNSUCCESSFUL (0xc0000001), per the
  pinned SDK `winsdk26100/shared/ntstatus.h:2065–2074`. It is generic, not
  a pool-floor or out-of-memory subreason. The CSV renders a message string;
  no raw ETL status extraction was performed here.
- `rec1951h/kmd-receipts-pre-disable.txt:162`, Wom1M2PoolDemand v8:
  SegmentBytes=1,409,286,144; PeakGranules=21,504 (exactly the full segment);
  PeakReservedBytes=3,035,758,592; CommitFailures=0, NewSlabFailures=0,
  ReserveFailures=0, FloorRefusals=0, ReleaseFailures=0. These are lifetime
  counters; current occupancy after cleanup is not occupancy at failure.
- Same file `:20`, decoded using tools/decode_ddi_status.py: no failures in
  CreateAllocation or DescribeAllocation. BuildPagingBuffer count 365,277;
  per-operation counts DiscardContent=57, VirtualTransfer=364,922,
  VirtualFill=298, all first statuses 0xc01e0001 (normal DMA-buffer retry).
  Internal transfer/fill slots agree. No UpdatePageTable/FlushTlb failure.
  Other nonzero slots: QueryDeviceDescriptor (2, c01e0401), QueryAdapterInfo
  (529, c00000bb), QueryInterface (22, c00000bb), Escape (136, c0000008),
  SubmitCommandVirtual (3, c000000d). Do not call this a clean DDI receipt.
  First-status capture can hide a different later status; counts and ETW,
  not the first status alone, carry the conclusion. Submit refusal is a
  separate possible device-error boundary, not the observed error 0xc.

The headline "single GPU-accessible segment" needs qualification: there is
already an aperture (segment 1), firmware memory (2), and pool (3). Explicit
GPU-only General allocations are restricted to the pool when present.
PlacementRestrictionNone does not override that KMD-supported segment set.

## Source contract and owners

- D/src/allocation_windows.c:519–538 reserves backing during CreateAllocation;
  :542–556 restricts GPU-only allocation placement to one native segment.
  :766–787 DescribeAllocation returns metadata and allocates no backing.
- D/src/render_pool_slab.c:22–24, :78–100: promises retain
  min(live reservation total, ReserveCap). D/src/pool_windows.c:262–303
  allocates 2-MiB slabs, falling back to checked contiguous 64-KiB chunks;
  :315–345 takes and maps held slots during paging. :389–397 applies the
  HighMemory && !LowMemory gate only to reservation growth; :521–552 calls
  reserve during creation. :812 matches ReserveCap to published capacity.
  Lowering the cap alone or bypassing the floor is not justified.
- CommitResource in the rc9 runtime study is a VidMm internal method, not
  a registered KMD DDI. The miniport receives CreateAllocation and paging
  callbacks. VidMm chooses placement and owns pageable system backing;
  the KMD owns pool MDLs, native translation and paging execution.
- D/src/memory_runtime_windows.c:9–14 already provides a 256-MiB aperture.
  D/src/memory_windows.c:3–18 preserves the validated budget groups (pool
  LOCAL, aperture NON_LOCAL); :130–204 describes the actual segment4 path.
- W/d3dkmddi.h:3912–3945 declares supported/preferred placement sets;
  :2081–2097 defines Use64KBPages on updates; :2113–2160 defines system
  64-KiB support. D/src/lifecycle.c:808,817 already enables that support and
  matching leaf storage. No new capability or reserved field is needed.
- D/src/gpuva_g3_paging_windows.c:594–676 expands a 64-KiB PTE into sixteen
  logical 4-KiB entries. :724–765 publishes only four equal-rights,
  physically consecutive PFNs with an aligned 16-KiB base and broker grant.
  :786–846 preserves logical pages and records omitted native groups.
  A contiguous 64-KiB allocation supplies four native leaves; a 4-KiB-mode
  mapping works only if each actual quartet satisfies the same predicate.
- Asahi primary source `.local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi/`
  Kconfig:18 requires PAGE_SIZE_16KB; mmu.rs:1200–1229 uses DMA SG addresses
  and rejects unaligned native mappings. Windows system PFNs need not share
  that property. No external implementation was copied.
- m1n1 owns broker grants/stage-2/TLB authorization; Mu owns boot reserves and
  ACPI. Their gitlinks are uninitialized here; live state inspection is
  explicitly forbidden. EXP1951's recorded m1n1 is 0d3a75b3; integration must
  preserve that exact launch contract and recovery. IRQ, power, reset, DMA
  authorization, CPU mapping and UMD fence owners are unchanged.

## Implemented first step and limits

Device REG_DWORD Wom1SysmemSpill=1 is read once at PASSIVE_LEVEL in pool start
(D/src/pool_windows.c:735–758,870). Missing, failed, malformed, 0 and other
values leave default behavior. D/src/allocation_windows.c:558–574 adds segment 1
as second preference and supported read/write placement only for an explicit
GPU-only General allocation with CpuVisible=0 and a pool reservation. Segment 3
remains preferred. No new segment, budget, caps, page size, signer or firmware
change; no allocation-time registry access. No CPU-visible or staged allocation
migration. A missing pool leaves the old firmware-only policy.

This removes that class's policy exclusion of overflow. It does not guarantee
successful rendering under arbitrary system backing. Existing 64-KiB support
is permission, not a mandatory-contiguity promise. Scattered backing stays
unpublished and fails native coverage before submit. No fake success, fabricated
physical adjacency, or new failure status in UpdatePageTable is introduced.
The rc12 general scattered-backing/coherence problem remains unsolved. This
opt-in experiment is specifically authorized by RC13; do not promote it to a
production hybrid implementation based on host tests or MakeResident success.

The allocation-life pool promise remains mandatory because pool placement is
still allowed. The first step can add placement capacity; it does not reclaim
those held pool pages or make them disk-pageable. The aperture is finite and
shared with existing users. No unbounded-overcommit claim is made.

## Verification and next falsifiable checkpoint

The new C allocation replay failed on old code at SupportedReadSegmentSet.
It now exercises switch on/off, pool present/absent, classes, CPU visibility,
explicit GPU-only intent, several ordinary sizes, and reservation failure.
Registry API substitution tests real reader code with absent, wrong-type,
wrong-length, failed and non-one inputs. Existing native-leaf replay checks
scattered/misaligned replacement invalidates the old leaf while preserving CPU
logical mappings. Host tests do not replace the external WDK compile gate.
Final commands, results, commit hashes and limitations are in RC13 REPORT.md.

Integration owner only: base 029fb709 plus RC13 commits, WDK compile and exact
package/manifest hashes first. Preserve Wom1GpuFaultInfoRead=1 and
Wom1PoolHalfSize=1. Compare Wom1SysmemSpill=0 versus =1 using separate adapter
starts; 2k then 10k, same trimming, CantTrimFurther and guard settings. Capture
DxgKrnl ETW, before/after DDI and pool receipts, native system-group/omitted-leaf
receipts, UMD logs, DWM errors and LOCAL/NON_LOCAL budget usage.

WHY THIS HYPOTHESIS: (1) the requested transfer exceeds remaining local capacity;
(2) pool backing/floor failures are zero and peak occupancy reached capacity;
(3) the KMD explicitly excludes aperture for GPU-only General allocations.

Success requires no Unrecoverable page in failure, actual GravityMark rendered
frames/completion, DWM alive, and native coverage/content correctness. Page-in
success followed by scattered-leaf refusal is a negative rendering verdict and
names the rc12 backing bridge as the next gate; never weaken coverage checks.
If the same page-in fails, determine whether the failed object belongs to the
opted-in class and whether aperture capacity/eligibility is exhausted. The
saved VidMm handle is not itself proof of its private GPU-only class flag.
Collect evidence before exact-package ordinary GPU-visible rollback using the
known-good recovery; no hardware run or preregistration was performed here.

Offline result: 521 tests in the required baseline, 6 known failures, 0 NEW,
BASELINE_OK. The extended targeted run passes 29/30; its sole error is
`test_apple_agx_render_pool_demand.RenderPoolDemandTest.test_broker_capacity_matches_m1n1`
because the m1n1_windows gitlink is uninitialized and hv_agx_gpuva_v5.h is absent.
No environment exception was added to known_failures.txt. The initial 22-test
placement/native-leaf/caps/header/headroom run passed completely.
