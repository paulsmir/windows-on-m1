# T7c: General BO placement for native UAT groups

## Decision and implementation plan

Choose TASK option (a): ordinary General-class allocations may be resident only in a
memory segment whose backing supports native 16 KiB leaves. In the pool profile,
that is segment 3 alone. Without the pool, use segment 2 alone. Preserve
CpuVisible, AccessedPhysically=0, SystemDirect by default, allocation alignment,
reservation accounting, and paging. Other classes retain their existing policy.
This is a KMD placement candidate, not a claim of Windows admission or hardware
validation. The existing comments cite EXP1027/EXP1266 rejecting CPU-visible
allocations without aperture support; the first hardware gate must test that.

1. Extend the real CreateAllocation body replay with General-class CPU-visible
   and invisible allocations across sizes, pool availability, and reservation
   failure; demonstrate RED for aperture fallback.
2. Remove aperture eligibility/preference for General BOs only, including the
   no-pool path. Keep class0 staging and shader/command class outputs unchanged.
3. Run the placement replay plus existing pool paging, trim/remap, SystemDirect,
   and native-leaf guards. Run `cd tests && python3 run_against_baseline.py`.
4. Record source evidence, limits and hardware checkpoint here; commit the
   reviewable change, then write the handoff REPORT.md with exact results.

## Source evidence and ownership

No new live measurements: this task explicitly forbids hardware/guest access.
The supplied T7b EXP1915 decode is the observation: General, CpuVisible=1,
129 MiB, LastSegment=0, valid writable logical PTEs with scattered 4 KiB IPAs,
and no publishable native leaf. The trace size is a regression input only.

KMD owns placement eligibility, contiguous pool backing and native leaf checks.
VidMm owns residency, GPUVA placement and paging requests; UMD owns references,
CPU locks and paging-fence waits. m1n1 owns broker/stage-2 mapping; Mu owns the
firmware reserve/ACPI exposure. No initialization, power, interrupt, firmware,
DMA protocol, or recovery contract changes are needed for this placement fix.

### Windows ABI and placement choice

- `.local/reference/wdk26100/d3dkmddi.h:3908` declares
  DXGK_ALLOCATIONINFO; `:3921`/`:3922` explicitly label MinimumPageSize and
  RecommendedPageSize as **inputs**, sharing storage with Alignment. They
  cannot be used as a KMD output request for contiguous aperture groups.
- That header `:2559` declares DXGK_SEGMENTFLAGS, including Use64KBPages
  (`:2578`); `:2605` declares DXGK_SEGMENTDESCRIPTOR, and `:2720` declares
  DESCRIPTOR4. None contains a per-General-allocation aperture-contiguity
  request. CpuTranslatedAddress is the physical CPU base when the segment
  is CPU visible without host-aperture support (`:2732`).
- DXGK_GPUMMUCAPS.SysMem64KBPageSupported exists (`d3dkmddi.h:2129`),
  but is already enabled by `shared/include/apple_agx_gpuva_g3_caps.h:89`
  and returned by `render-admission/src/lifecycle.c:808`; the INF already
  sets MemoryManager\Supports64KBPages (`:93`). These paths are under
  `drivers/apple-agx/`. Advertising support is not a mandatory backing guarantee.
  The existing `investigation/analysis/M2-vidmm-64k-decision.md:92` addendum
  records EXP1240-EXP1244 scattered system backing despite the 64 KiB opt-in.
  Its earlier binary analysis also identifies CPU-visible memory-segment
  membership as a restriction. Those historical findings are cited as prior
  evidence, not freshly reproduced facts. R71 is therefore not a missing flag
  to enable in this change.
- WDDM 3.2 does have a SlabSize in DESCRIPTOR5 (`:11102`/`:11121`) and
  DXGK_PAGESIZE_16KB (`:11086`). This is a segment-level interface, not a
  PreferredSegment alignment field or a demonstrated guarantee for these
  system allocations. Changing the system segment/page-size contract would
  affect class0 and other classes and repeat earlier admission experiments.
  No such global change is justified by this General-class failure.
- `.local/reference/winsdk26100/shared/d3dukmdt.h:738` defines
  D3DDDI_SEGMENTPREFERENCE: five segment IDs and directions, no page-size or
  physical-contiguity field. d3dkmddi.h:50 aliases DXGK_SEGMENTPREFERENCE.
  There is no GetSegmentPreference implementation in this driver's source;
  CreateAllocation returns the policy directly.
- Microsoft's local primary sample,
  `.local/reference/microsoft-graphics-driver-samples/render-only-sample/roskmd/RosKmdAdapter.cpp:823`,
  uses EvictionSegmentSet=0 with the comment “don't use apperture for eviction”.
  At `:825`-`:854`, unshared non-present allocations are CPU-visible and have
  only the video-memory segment in their preferred/read/write sets. It also
  explicitly distinguishes shared/present allocations. This supports option
  (a) as a Windows allocation pattern, **not** proof of this WDDM/GpuMmu
  profile's acceptance. No external code was copied.

### Native grouping and backing

- `.local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi/pgtable.rs:39`
  defines UAT_PGBIT=14 and `:41` defines UAT_PGSZ. `asahi/mmu.rs:1090`
  obtains a GEM scatter/gather table and maps native-page-sized ranges.
  `asahi/gem.rs:163` rounds the GEM size to UAT_PGSZ. The Windows mismatch is
  representability of four independent 4 KiB pages, not BO byte size alone.
- `.local/reference/mesa/src/asahi/lib/agx_bo.c:355` rounds BO size and minimum
  alignment to AIL_PAGESIZE before invoking the OS allocator (`:379`). Those
  virtual/size requirements do not impose physical adjacency on Windows PFNs.
- `drivers/apple-agx/render-admission/src/gpuva_g3_windows.c:975`-`:1004`
  checks native groups for full valid entries, matching attributes, 16 KiB
  alignment and consecutive IPAs. T7b's supplied EXP1915 decode observes
  failure of that physical condition despite successful logical updates.
- `drivers/apple-agx/render-admission/src/pool_windows.c:261`-`:298`
  allocates fully required contiguous chunks and checks every PFN in each
  demand granule. Segment 3 thus provides representable native groups even
  when distinct 64 KiB granules are scattered.
- `drivers/apple-agx/render-admission/src/memory_windows.c:21`-`:36` emits
  segment flags; `:188`-`:196` provides the pool CPU-translated base.
  No change to these flags, mappings or budget groups is required here.
- The assigned worktree's m1n1 and Mu submodules are uninitialized (pins
  21c3648d32423def46086e258e0995c737dc9284 and
  361a2134b2f742f331f8d75ae5f60ad4f7f413f1). Their current implementations,
  generated ACPI and live launch state were not revalidated. The fix depends
  on the existing KMD pool's checked-granule contract and changes no broker,
  stage-2, firmware, power, interrupt or launch behavior. That limit is explicit;
  this offline task is not evidence for a new assisted/standalone contract.

### Translation, eviction and restoration

`drivers/apple-agx/render-admission/src/allocation_windows.c:497`-`:539`
now gives ordinary General BOs exactly one eligible memory segment: read/write
mask 4 and preference 3 with a pool, mask 2 and preference 2 without it.
There is no size threshold and no dependence on trace content/binds. The
pool reservation still precedes publishing the allocation handle; refusal is
STATUS_NO_MEMORY (`:519`). EvictionSegmentSet stays zero, CpuVisible retains
the caller's validated description, and AccessedPhysically remains false for
native classes (`:537`-`:551`). Destruction unreserves the allocation at `:645`.

Class0 staging remains aperture-only. CPU-visible shader/command classes keep
their prior policy, outside this General-only change. Explicit R105 diagnostic
requests can still replace the outputs via AdmissionR105Apply (`:16`-`:36`,
called at `:556`); they are intentionally outside the ordinary-request invariant.
The replay stubs this experimental decoder false.

- Physical BuildPagingBuffer TRANSFER dispatch in `src/paging_windows.c:82`
  obtains the system MDL and uses AdmissionMemoryPlanTransfer (`:101`).
  `src/render_memory.c:312`-`:345` accepts exactly one system endpoint and
  resolves the other memory segment, including 3. The executor in
  `src/memory_runtime_windows.c:1087` selects the pool CPU view and copies
  against the system MDL. GPU-resident eligibility is distinct from storage
  used while evicted: system backing need not publish a native GPU leaf.
- Virtual paging in `src/gpuva_g3_paging_windows.c:991`-`:1029` resolves
  logical source/destination VAs and treats segment 3 as local for both
  SYSTEM_TO_LOCAL and LOCAL_TO_SYSTEM. This code is unchanged.
- `umd/src/umd_gpuva_windows.c:1268`-`:1301` restores the same kernel
  allocation with MakeResident, waits for its paging fence and reserves
  Transition before LockCb. VidMm must select one of that allocation's legal
  segments, hence segment 3 in the pool profile. `umd/include/umd_residency_trim.h:23`
  excludes mapped/submitted/held slots from trim; idle SystemDirect remains
  evictable. The KMD does not make SystemDirect permanently resident.
- `umd/src/umd_win32_screen.c:574` retains the C75/T6b default
  `!BorrowedStaging && Bytes <= classInfo->MaximumBytes`; `:17` selects the
  same canonical allocation for CPU access. No UMD file changed.

Paths beginning `src/` or `umd/` above are relative to
`drivers/apple-agx/render-admission/`.

## Verification

The new regression in `tests/test_g4_segment_placement_replay.py:410` compiles
and executes the real CreateAllocation body. It tests both page profiles,
CPU-visible/invisible General BOs, pool present/absent, 64 KiB through 256 MiB
(including 16 MiB boundaries and the 129 MiB trace case), reservation refusal,
CPU mapping flags, eviction set and unaffected shader/command allocations.
Existing replay coverage retains class0 and invalid native size checks.

RED: original source fails both page-profile subcases on aperture eligibility;
updated existing replay expectations fail on the aperture preference. GREEN:
all three replay methods pass. OS allocation/reservation are boundary stubs,
so this proves KMD output policy, not Windows placement or CPU-map acceptance.

The initial targeted run passed 24 of 25 tests; the remaining test,
`test_apple_agx_render_pool_demand.RenderPoolDemandTest.test_broker_capacity_matches_m1n1`,
could not read the absent submodule's `src/hv_agx_gpuva_v5.h`. This was not
suppressed or added to known_failures. Pool granule/slab tests, portable paging
plans/executor, SystemDirect policy, trim and CPU remap replay passed.

Required baseline result:

```text
ran 393 tests in 169 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

The dependency-free targeted rerun passed all 24 tests. The absent broker
capacity test remains unverified, not skipped into a claim of 25/25.

No package, Windows driver build, hardware or guest operation was performed.
Independent read-only review passed the placement replay and found no code
blocker; its two documentation qualifications are included above.

## WHAT IS STILL UNKNOWN / smallest hardware checkpoint

Windows acceptance is the first gate, **before GravityMark**. Prior
EXP1027/EXP1266 memory-only CPU-visible rejection remains contrary evidence;
this replay does not resolve it, and the old sample uses a different profile.
Claude should preregister only this KMD placement variable, retaining the
known-good C75/T6b SystemDirect UMD and existing firmware/caps/signer/recovery.

1. Create an ordinary unshared CPU-visible General BO, CPU Lock/write/Unlock,
   make resident, and run one native GPU read/write plus CPU readback. Check
   AllocateCb/MakeResident/Lock statuses and the allocation's segment provenance.
   Start with 64 KiB; test a larger admitted buffer only after this gate passes.
2. Trim the idle BO, induce real eviction under bounded pressure, remap it and
   verify content after the paging fence. Inspect 3→0→3 transfer provenance:
   zero G4 unpublished system-group refusal for this BO, and residency returns
   to segment 3. Clean OOM is acceptable if capacity cannot be recovered; a
   successful resident system/scattered mapping is a failure.
3. Then rerun the GravityMark D3D12 boundary and compare D3D11/DWM direct-buffer
   behavior. Peak memory, VidMm placement under pressure, performance, and
   shader/command fallback behavior remain hardware questions.

If admission/Lock is rejected, stop before the workload and retain the exact
status/receipt: do not infer success or silently re-enable aperture/copy staging.
Use the existing exact-package rollback and known-good GPU-visible recovery;
GPU-hidden recovery remains emergency-only. No recovery action was run here.
