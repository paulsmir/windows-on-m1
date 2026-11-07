# RC18: original/native identity prerequisite; shadow remains disabled

Offline on codex/rc18, base 6080221b. This is the smaller safe sub-step permitted
by TASK.md item 3, not a complete shadow-backed spill implementation or a fix
for GravityMark paging thrash. No Wom1SysmemShadow switch is added. Do not enable
Wom1SysmemSpill based on this branch.

## Implemented contract

`drivers/apple-agx/shared/include/apple_agx_gpuva_g3_translation.h:33` retains
GuestIpa as the original paging endpoint and adds NativeIpa as KMD resident
metadata. NativeIpa is meaningful in ResidentPtes, not LogicalPtes or the shared
span-planning input. It is neither a broker grant nor an allocation owner.

`render-admission/src/gpuva_g3_paging_windows.c:820` (paths below are under
`drivers/apple-agx/`) records native identity only after the existing graph
publication succeeds. All four entries in the affected 16 KiB group change,
even for a one-PTE update. Unrepresentable/refused groups get zero native
identity; rejected transactions do not commit the candidate ResidentPtes.
Original paging addresses, provenance and reference release remain unchanged.

`render-admission/src/gpuva_g3_windows.c:1443` still authenticates canonical
allocation, allocation offset and permissions. At :1452 it checks the graph
against NativeIpa. **Direct backing is still mandatory:** NativeIpa must also
equal GuestIpa. Removing that equality is forbidden until a live shadow owner,
generation, rights and ordered content authority replace the direct proof.
Missing/stale native identity fails closed. No caller-selected address or size
can authorize a shadow.

`render-admission/src/gpuva_g3_paging_windows.c:973` resolves CPU paging from
GuestIpa; :1022 encodes address records and :1257 executes their copies later.
The new host replay uses these actual functions and the actual mapper/fill
helper. Distinct native metadata cannot redirect CPU copies; building a copy
before a queued fill executes does not snapshot stale bytes. Both transfer
directions are checked against byte arrays substituting for OS memory maps.
This is not a Windows scheduler, GPU, broker or shadow-lifetime emulation.

NativePte metadata adds 8 bytes per logical entry: 64 KiB per 8192-entry array,
128 KiB for a logical/resident pair, plus 64 KiB while a candidate is present.
This overhead is a material limitation under the memory pressure motivating
RC18. The wire PTE, native graph, paging record and query ABI are unchanged;
all code using the internal struct must rebuild together. No WDK build was run.

## First unclosed gate and remaining implementation

1. **Bounded storage with exclusive ownership.** `render-admission/src/`
   `render_pool_slab.c:22` caps a quantity promise at ReserveCap; it does not
   reserve an address. `render_pool_demand.c:75` lets VidMm placement replace
   overlapping owners. `pool_windows.c:800` exposes the demand pool and sets
   its matching ReserveCap at :850. A shadow must not borrow a currently empty
   advertised slot. The inspected path has no private storage domain. Select
   a disjoint window partition excluded from segment advertisement, or a real
   companion allocation with residency/budget ownership, and reserve at an
   allowed fallible boundary. Raw slab PFNs are another possible storage route,
   but require their own broker/grant, cache and capacity review; taking slab
   slots alone does not preserve existing paging promises.
2. **Ordered content authority.** `paging_windows.c:340` executes previously
   built records; build-time copy-in can precede a queued fill. Add activation
   records and a canonical allocation/range generation owner, then route both
   virtual and physical copies/escapes through it. Original page lifetime must
   survive execution; snapshots alone do not own those pages.
3. **Copy-out, alias and teardown.** Drain/revoke every native alias before
   recycling private backing; retain the original generation through copy-out
   and fence completion. Partial unmap is not discard permission. Preserve
   backing on uncertain revoke/reset. GPU-only intent excludes ordinary CPU
   mappings, but not paging reads and writes. None of these shadow lifecycle
   mechanisms is implemented by the added NativeIpa field.
4. **Enablement.** Only after actual production replays cover those gates may
   Wom1SysmemShadow=1 imply spill for GPU-only General allocations. Default,
   CPU-visible, non-General and existing direct-map behavior must remain intact.

The supplied RC14 contract describes these blockers; this branch does not claim
that a complete KMD shadow is impossible. It stops at the first storage/authority
gate rather than making native coverage succeed over unowned or stale bytes.

## Source and validation boundaries

Pinned WDK `.local/reference/wdk26100/d3dkmddi.h:4693` and :4741 supply update
allocation/offset and transfer VA/page-table fields. Asahi's
`.local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi/pgtable.rs:39`,
`Kconfig:18`, and `mmu.rs:1200` / :1221 establish 16 KiB leaves, Linux's native
page-size requirement and aligned DMA ranges. Linux's shmem contract does not
supply Windows with contiguous 4 KiB PFN quartets. No external code copied.

VidMm owns placement and originals; KMD owns metadata, copies and native
publication; UMD owns residency/fences. m1n1 owns grants/stage-2, Mu boot reserves
and ACPI. IRQ, power and recovery owners are unchanged. The uninitialized
m1n1/Mu gitlinks and prohibition on live/other-worktree access prevent verifying
current launch state; no firmware or hypervisor behavior is proposed here.

EXPERIMENTS.md:63944 records incomplete native coverage with spill (EXP1952).
:63980 records the 2016 MiB operating point with ~5–6 scene frames/minute
(EXP1954). :63995 records full-pool load-time OOM (EXP1955). These motivate a
backing bridge, not a change to the native guard or another capacity flag.

Host regression results and exact commit IDs are in the RC18 handoff REPORT.md.
Leaf identity and authenticated-query tests were observed RED then GREEN; the
paging identity replay also catches a deliberate GuestIpa-to-NativeIpa mutation.
Full firmware-dependent paging replays cannot run without the absent m1n1 source.

## Conditional checkpoint for the integration owner

No hardware run is justified solely by this metadata prerequisite. After the
remaining bridge and pinned WDK compile: base HEAD + completed fix, switches
Wom1GpuFaultInfoRead=1, Wom1PoolFraction=3, Wom1SysmemShadow=1 (future switch).
First prove one scattered GPU-only allocation: queued initialization, native
read/write, two aliases, eviction/copy-out, readback/remap, exhaustion refusal
and acknowledged teardown. Then GravityMark D3D12 2k, followed by 10k.
Expected signals: no reject-native-allocation, correct bytes, fewer slow frames,
and DWM alive. Record original/native owners, copies/fences, pool/system budget,
ETW and rendered-frame timing. Use exact package hashes and ordinary GPU-visible
recovery after collecting evidence. No package, run or preregistration here.
