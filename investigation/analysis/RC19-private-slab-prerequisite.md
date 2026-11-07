# RC19: bounded raw native storage prerequisite

This is the largest safe sub-step selected under RC19 TASK.md's stop allowance.
It is **not a completed shadow spill path**. No production caller acquires a
private lease, no registry switch is added, and placement is unchanged.
Gate 1 has an allocator primitive; admission/mapping integration is still open.

## Contract and implementation

The native pool already obtains verified contiguous, 64-KiB-aligned 64-KiB slots
from locked Windows MDLs (`render-admission/src/pool_windows.c:262-303`; paths
below are under `drivers/apple-agx`). Each slot can contain four native 16-KiB
leaves. RC19 retains that allocation granularity; it does not introduce a
16-KiB suballocator or borrow any advertised segment offset.

`render-admission/include/render_pool_slab.h:55` defines a zero-initialized,
address-stable private lease. `src/render_pool_slab.c:157` acquires one raw slot
with a caller-configured cap in slots (zero disables acquisition). The caller
holds the same pool lock used for normal take/give/reserve. The slab records the
exact lease address as owner; another lease, including a copied token, cannot
release it. Normal Give cannot release private slots (:73). GivePrivate (:187)
finds the slab in the pool before dereferencing a potentially stale pointer,
checks the owner, clears the lease and returns the slot. This is storage
ownership, not allocation-generation or GPU-reference ownership.

`src/render_pool_slab.c:22` now retains at least
`min(Reserved, ReserveCap) + PrivateCount` slots. Both native reservation and
private acquisition check addition overflow. Private acquisition also counts
currently live normal slots, which can survive unreservation. It obtains enough
backing before taking a slot and checks MayGrow on every new slab, so it cannot
consume the native promise or invoke the ungated paging growth path. Cap/floor/
allocation failure leaves the lease inactive. Successful acquisition does not
publish an address or a PTE. Failure is boolean, to be translated at a supported
fallible allocation boundary by future integration; there is no new WDK OOM or
fallback behavior in this branch.

Drain (:207) retains the entire pool while a private lease exists. This prevents
local premature freeing; it does not make existing adapter stop code safe for
active shadows. The future runtime must drain copies/revoke GPU aliases, release
leases and retain the pool object/MDLs on uncertain revoke before calling its
existing free path. Current production PrivateCap/PrivateCount remain zero.

Cost: 32 owner pointers per slab (256 bytes on ARM64), 16 bytes per pool, and
16 bytes per lease on ARM64. Private backing adds locked RAM beyond the native
promise; it is not a memory-pressure performance fix. Slab rounding and packing
can retain more physical RAM than leased bytes. The cap bounds leased 64-KiB
slots, not a separate whole-slab RAM budget. Future policy must account for this.

## Source contract and stop gate

- Asahi `pgtable.rs:39-43` and `mmu.rs:1200-1229` in the local primary reference
  establish 16-KiB leaves and aligned DMA SG mappings. Windows 4-KiB originals
  need not share that adjacency. No external implementation was copied.
- WDK26100 `d3dkmddi.h:4693-4752` supplies allocation/offset and transfer fields.
  It does not make raw private storage a pageable allocation or GPU grant.
- `src/gpuva_g3_paging_windows.c:973` resolves original logical backing; :1022
  builds records; :1257 executes copies later. `src/paging_windows.c:340`
  executes queued records sequentially. Copy-in at table-update/build time can
  precede a queued source fill. A lease does not establish content authority.
- `src/gpuva_g3_windows.c:1443-1454` continues authenticating native mappings
  against direct backing. Do not remove the GuestIpa/NativeIpa equality until
  canonical shadow generation and ordered activation replace that proof.
- `src/pool_windows.c:800-850` publishes segment capacity and its matching
  promise. RC19 changes neither and does not steal a window address from VidMm.

VidMm owns original pages/placement; KMD owns MDLs, storage and paging execution;
UMD owns submission fences. m1n1 owns stage-2/grants and Mu boot reserves/ACPI.
Initialization, interrupts, DMA publication, power and recovery are unchanged.
The m1n1/Mu gitlinks are uninitialized (21c3648d/361a2134); live state and other
worktrees are forbidden. This offline change certifies no launch/cache contract.

Remaining gates, in order:

1. Integrate bounded raw storage at fallible canonical allocation admission,
   with all-or-nothing multi-slot acquisition and truthful native fallback/OOM.
   Choose a policy cap and account for locked RAM/broker capacity. Establish
   private CPU cache mapping and GPU grants using the exact current platform.
2. Add execution-ordered copy-in/publication and copy-out. Hold originals and
   private backing through pending records; route virtual/physical paging and
   copy escapes through the same content authority.
3. Canonical allocation/range/generation ownership shared by aliases/placed
   resources; partial PTE changes must not reinitialize live GPU-written bytes.
4. Destroy/evict/device-removal/process-exit/reset integration, with acknowledged
   revoke before release and retention on uncertainty.
5. Only then read Wom1SysmemShadow once at adapter start, default off, implying
   spill only for GPU-only General BOs with no CPU mapping.

## Offline evidence and future checkpoint

`tests/test_render_pool_private.py` executes the production slab allocator under
ASan/UBSan. It covers cap/floor/OS refusal, native promises before and after
private acquisition, live normal references without reservations, copied/foreign
release, normal Give protection, drain retention and overflow.
`tests/test_g4_paging_backing_identity.py` links the allocator into the actual
resolver/encoder/mapper/executor replay: originals remain the CPU endpoints,
queued fills precede transfers, both directions consume execution-time bytes,
and distinct leased native bytes stay unchanged. OS mapping/slab acquisition
are substitutes; this does not simulate GPU execution or shadow activation.
Mutation checks reject removing the private floor and resolving CPU paging via
NativeIpa. Exact results, limitations and commits are in the RC19 handoff report.

Do not run this sub-step alone as a shadow fix. After the remaining bridge and
WDK compile: **base HEAD + completed fix**, switches **Wom1GpuFaultInfoRead=1,
Wom1PoolFraction=3, Wom1SysmemShadow=1**, GravityMark **2k then 10k**. First prove
one measured scattered GPU-only allocation with queued initialization, two
aliases, GPU writes, eviction/copy-out, exact readback/remap, cap refusal and
acknowledged teardown. Require no reject-native-allocation for spilled BOs,
faster scene frames than EXP1954 and DWM alive; residency success is insufficient.
The integration owner records artifact hashes/recovery and performs ordinary
exact-package GPU-visible cleanup after evidence collection. No run was made or
preregistered here.
