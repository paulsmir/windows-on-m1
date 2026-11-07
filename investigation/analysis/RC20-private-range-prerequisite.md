# RC20: atomic private allocation storage prerequisite

This is a safe sub-step under RC20 TASK.md's stop allowance, not the completed
shadow spill path. Gate 1 now has all-or-nothing multi-slot acquisition. There
is still no production caller, allocation-admission integration, ordered shadow
activation, or Wom1SysmemShadow switch. Do not run this branch as a shadow fix.

## Contract and evidence

Paths below are under drivers/apple-agx/render-admission unless noted.

- include/render_pool_slab.h:66 and src/render_pool_slab.c:187 add
  AdmissionPoolSlabTakePrivateRange. The caller supplies a zeroed, stable-address
  array and holds the pool lock for the whole transaction. A nonzero count must
  fit the remaining private cap and combined promise without overflow. All
  entries are checked before acquisition; any active/nonzero entry is refused.
- Acquisition reuses the exact-owner single-slot primitive (:157). On any late
  refusal, the acquired prefix is released in reverse through GivePrivate
  (:211). Existing leases and native promises survive. No temporary live lease
  is copied. Success means all entries are owned, not that a GPU can use them.
- src/pool_windows.c:262-303 checks locked MDL slots for 64-KiB contiguity and
  alignment; every slot contains four 16-KiB native leaves. The entire range is
  not physically contiguous. The cap bounds leased slots, not whole-slab RAM;
  spare retention and packing can retain additional physical storage.
- src/allocation_windows.c:522-530 is the existing fallible allocation reserve
  boundary returning STATUS_NO_MEMORY. The new helper is not called there yet:
  acquiring raw slots without a matching lifetime owner is insufficient.
- src/gpuva_g3_paging_windows.c:973, :1022, :1257 resolve original backing,
  encode copies, then execute later; src/paging_windows.c:340 orders records.
  tests/test_g4_paging_backing_identity.py executes those real functions with
  two distinct private leases. Both originals remain CPU endpoints; a queued
  fill precedes the transfer, and the reverse copy reads execution-time bytes.
  Both private slots remain untouched. This is not shadow/GPU execution.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A failed whole-allocation
acquisition must not strand its successful prefix, erase existing ownership,
or consume native backing promises. The real allocator test forces failure
on a later slab from both OS refusal and the memory floor, then checks every
lease/count/promise and subsequent success. It checks zero/null/maximal count,
active input and exact-cap success. Removing rollback causes an assertion
failure. Host builds use ASan/UBSan, -Wall -Wextra -Werror. No KMD CRT headers.

## Why runtime integration stops here

1. Private slab MDLs use MmWriteCombined (pool_windows.c:279), while the generic
   system paging mapper uses MmCached (gpuva_g3_paging_windows.c:1227). Passing
   raw private addresses through that generic mapper is not a verified cache
   contract. Implement a correctly attributed MDL mapping and establish raw
   page grant authorization before publishing native addresses.
2. Pool shutdown drains then frees the containing runtime (pool_windows.c:653,
   :658). SlabDrain's private guard alone cannot preserve this owner. Adding an
   allocation caller now requires adapter/reset/process teardown integration,
   retention of the containing owner on uncertainty and acknowledged revocation.
3. An address lease does not define content authority. Allocation/range/generation
   ownership, shared aliases, original-page holds, ordered copy-in/publication,
   copy-out and all virtual/physical/copy-escape consumers must agree before the
   direct-only native query at gpuva_g3_windows.c:1452 may change.

These are source-backed missing integrations, not proof that a full bridge is
impossible. No novel OOM or retry is introduced at UpdatePageTable. Existing
placement, native guard, direct maps, capabilities and pool sizes are unchanged.

## Primary sources and ownership

Pinned .local/reference/wdk26100/d3dkmddi.h:4693-4752 supplies allocation,
offset and transfer fields. Asahi pgtable.rs:39-43 and mmu.rs:1200-1229 under
.local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi define native 16-KiB
leaves and aligned DMA ranges. They do not guarantee Windows PFN adjacency.
No external code was copied. m1n1/Mu gitlinks are uninitialized here (21c3648d /
361a2134); current broker grants/cache and generated ACPI remain unverified.
Live state and other-worktree access were prohibited. No launch claim is made.

VidMm owns originals/placement, KMD owns private MDLs/storage/copies/publication,
UMD owns submission fences, m1n1 owns grants/stage-2 and Mu owns boot reserves.
Initialization, interrupts, power and recovery ownership do not change.

## Remaining work and conditional checkpoint

Gate 1 still needs canonical allocation admission, a fixed physical budget/cap,
matching cache mapping and broker authorization, with native fallback or truthful
OOM. Gates 2-4 need ordered copies, shared generation ownership and complete
retirement on destroy/evict/reset/device-removal/process-exit. Gate 5 comes last:
read Wom1SysmemShadow once at adapter start, default off, implying spill only
for GPU-only General BOs with no CPU mapping. This helper enables none of these.

After the completed bridge and pinned WDK build: base HEAD + fix, switches
Wom1GpuFaultInfoRead=1, Wom1PoolFraction=3, Wom1SysmemShadow=1. First test one
measured scattered allocation with queued initialization, two aliases, GPU
writes, eviction/copy-out, exact readback/remap, cap refusal and acknowledged
teardown. Then GravityMark D3D12 2k, then 10k. Expected signals: no
reject-native-allocation for spilled BOs, faster scene frames than EXP1954,
and DWM alive. Retain exact hashes/recovery and collect evidence before cleanup.
No hardware run, package or preregistration was performed here.
