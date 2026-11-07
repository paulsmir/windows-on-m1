# RC12 — flexible GPU memory: contract and staged migration

2026-10-25; offline source study on codex/rc12, base 651a86c4.
D = drivers/apple-agx; W = .local/reference/wdk26100;
A = .local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi.
Line coordinates refer to this branch. No hardware or network research was run.

## Decision

64-KiB system backing is **possible and already supported**, but the inspected
WDK contract does not provide a KMD switch guaranteeing that *every* GPU-accessed
system allocation has contiguous 64-KiB backing. That distinction prevents a safe
caps-only migration. The suggested Wom1Sysmem64K switch would duplicate existing
SysMem64KBPageSupported and the INF opt-in. Do not add it, turn on unsupported
LargePage modes, or advertise unrestricted system-memory residency.

The current pool is already dynamically physically backed. Its fixed *address
capacity* is not its physical allocation. The restriction that matters is the
physical promise retained for live allocations, plus the mismatch between 4-KiB
Windows backing and 16-KiB AGX leaves. A larger reservation alone solves neither.

Recommended architecture: retain a small guaranteed native pool for tables,
firmware and other nonpageable consumers; move ordinary BO storage to a
VidMm-managed pageable backing path only after native accessibility and CPU/GPU
coherence are proven for both contiguous and scattered placements. A hybrid is
the practical migration, not a declaration-only replacement. Persistently mapped
CPU-visible resources are the blocking case for a transparent shadow design.
Until that contract is solved, retain their current supported behavior and report
the limitation honestly. This proposal does not claim macOS-like flexibility is
implemented or GravityMark is fixed.

First safe code change delivered: checked addition in the pool reservation
accounting. It prevents an oversized request from wrapping an existing promise
away. It is a prerequisite for reasoning about expanded reservations, not the
cause of the observed 2–3 GiB pressure failures. No current app reachability of
64-bit overflow has been established.

## Evidence and owner boundaries

| Evidence | What it establishes |
| --- | --- |
| investigation/EXPERIMENTS.md:53196–53197 (EXP1027), :61552–61558 (EXP1917) | CPU-visible General allocations restricted to local memory were refused with E_INVALIDARG. Removing aperture fallback is not an admitted solution. |
| Same ledger :61991–62090 (EXP1927) | Half-size segment avoided reserve/floor failures, but MakeResident exhausted its budget and GPU faults persisted. Half size did not establish a usable workload. |
| Same ledger :63769 (EXP1940) | Full 2688-MiB segment: 17 reserve failures, all floor refusals; earlier resource-OOM boundary, no new-slab/commit failure. Not proof of a paging failure or an adapter-wide removal. |
| Handoff rc8/REPORT.md, Evidence and chronology | A CPU-visible Encoder has four valid but nonadjacent system PFNs. Native backing cannot be inferred from VA alignment, object size, or residency success. |
| Handoff rc9/RE-dxgkrnl.md:170–181, :218–259 | Paging failure and per-device removal are different boundaries; the first GM removal site remains unresolved. The x64 runtime disassembly is an inference for ARM64, not primary source for a new DDI contract. |
| D/render-admission/src/pool_windows.c:262–303, :315–385 | 2-MiB slabs try a contiguous chunk, then 64-KiB contiguous chunks; each PFN run is checked. Window mapping and unmapping precede slot publication/freeing. |
| D/render-admission/src/render_pool_slab.c:22–30, :78–105 | Held backing covers min(live reservations, ReserveCap); returning slots alone cannot release that promise. |
| D/render-admission/src/pool_windows.c:390–414, :527, :692–701, :786 | Headroom gate, pressure reclaim, creation reservation, fixed-at-start size and matching ReserveCap. Lowering only the cap would break admitted paging. |
| A/Kconfig:18; A/pgtable.rs:39–43; A/gem.rs:57, :160–174; A/mmu.rs:1200–1229 | Linux requires PAGE_SIZE_16KB, uses GEM shmem, DMA SG addresses and 16-KiB-aligned UAT mappings. Ordinary Linux pages already satisfy the native granularity. |

VidMm owns placement, pageable system backing, residency budgets and eviction.
The UMD owns submission holds, CPU maps and fences. The KMD owns its MDLs,
logical-to-native translation, paging execution and pool-window lifetime. Asahi
owns resource/batch dependencies, not Windows PFN layout. m1n1 owns stage-2 and
broker authorization; Mu owns boot memory/ACPI exposure. Existing firmware,
power, interrupt, reset and recovery owners remain unchanged.

Live ADT/register/interrupt/launch inspection is forbidden by this task. Both
m1n1_windows and mu are uninitialized gitlinks (21c3648d and 361a2134 respectively),
so current implementation and generated ACPI cannot be reverified here. Saved
EXP1917/1927 contracts identify m1n1 0d3a75b3; this differs from the gitlink and
must be resolved by the integration owner before a migration experiment. No
firmware or hypervisor code is proposed in this branch. No external code copied.

## WDK 26100 contract: what each flag actually buys

The pinned declarations are the offline ABI authority. Existing source studies
T7e-sysmem-page-size.md and M2-vidmm-64k-decision.md contain Microsoft reference
links and historical image-specific analysis; their stronger runtime conclusions
are labeled as historical evidence, not guarantees from a structure field name.

| Declaration / quote | Required handling and limit |
| --- | --- |
| W/d3dkmddi.h:2113–2160, `SysMem64KBPageSupported`, `LeafPageTableSizeFor64KPagesInBytes` | Advertise supported system 64-KiB leaf mode and matching table storage. Already set at D/render-admission/src/lifecycle.c:808, :817; enabled model sets it at D/shared/include/apple_agx_gpuva_g3_caps.h:89. Capability is support, not a no-fallback allocation request. |
| D/render-admission/AppleAgxRenderAdmission.inf:88–93, `Supports64KBPages` | Existing adapter registry opt-in from the M2 runtime investigation. This is not a new public per-allocation WDK flag, nor proof that every installed package's registry is correct. |
| W/d3dkmddi.h:2071–2086, page-table descriptors and `Use64KBPages` | Table index bits, segment, byte size/alignment and per-update mode must agree. Existing model has 13/11/3 bits, 39 VA bits; 64-KiB leaf storage 0x4000. Update decoder expands a 64-KiB entry into sixteen logical 4-KiB entries (gpuva_g3_paging_windows.c:594). |
| W/d3dukmdt.h:305–339, leaf modes and `LargePage` | Logical PTE PageAddress is in 4-KiB units; leaf-table mode and a non-leaf block entry are distinct. Four independently checked 16-KiB native leaves can represent one aligned physical 64-KiB run. |
| W/d3dkmddi.h:2558–2600, `Aperture`, `CpuVisible`, `Use64KBPages`, `PopulatedFromSystemMemory` | Aperture describes an address mapping of system backing. Use64KBPages does not itself allocate RAM or guarantee physical contiguity. PopulatedFromSystemMemory describes driver-owned RAM, not a switch transferring ownership of the private pool to VidMm. ReservedSysMem stays reserved. |
| W/d3dkmddi.h:2605 (DXGK_SEGMENTDESCRIPTOR), :2684–2701 (descriptor3) | Size is address capacity, CpuTranslatedAddress is the CPU-visible base; descriptor3 says CommitLimit applies to aperture. These fields do not provide a runtime grow/shrink callback. Production uses descriptor4 (memory_windows.c:130–204), so do not implement the migration only in obsolete descriptor3. |
| W/d3dkmddi.h:2125–2133, `LargePageSupported`, `SysMemLargePageSupported`, `AllowNonAlignedLargePageAddress` | Larger-block/large-page capabilities are separate from 64-KiB leaf mode. Current decoder refuses LargePage at gpuva_g3_paging_windows.c:646; the parent/table geometry and splitting machinery are not a large-block implementation. Leave these caps zero. |
| W/d3dkmddi.h:3799–3840, DXGK_ALLOCATIONINFOFLAGS_WDDM2_0 | CpuVisible, PermanentSysMem, ExistingSysMem, AccessedPhysically and MapApertureCpuVisible describe access/ownership, not mandatory 64-KiB PFN runs. DisableLargePageMapping being zero permits supported mappings; it does not demand large backing. No reserved bit may be used as a discovered internal forcing flag. |
| W/d3dkmddi.h:3921–3922 | `MinimumPageSize` and `RecommendedPageSize` are marked `in`, sharing storage with Alignment. Do not write page-size enum values into the normal WDDM 3.0 Alignment output as a new contract. |
| W/d3dkmddi.h:4727–4752, `Src64KBPages`, `Dst64KBPages` | These are TransferVirtual source/destination page-table interpretation hints. TransferSizeInBytes and AllocationOffsetInBytes remain bytes. They do not request either backing allocation or a whole-64-KiB copy. Legacy Transfer flags at :4632 are a separate type. Existing RC8 encoder accepts the defined bits and rejects Reserved at gpuva_g3_paging_windows.c:1043–1047. |

For a conforming 64-KiB path, retain GpuMmu adapter/node declarations, appropriate
leaf storage/level descriptors, the system support bit and actual decoder,
64-KiB-aligned eligible allocations, supported segment page mode, transfer-mode
handling, native authorization and TLB ordering. Do not enable DualPte or newer
unavailable bits. tests/test_g4_sysmem_page_caps.py already executes the real
query against pinned WDK types for WDDM 3.0 and 3.2.

M2's older-image disassembly says mixed CPU-visible aperture/local segment sets
can exclude allocations from 64-KiB eligibility. Its addendum reports scattered
backing even after aperture-only opt-in (EXP1244). RC8 independently supplies a
later scattered Encoder. Thus physical contiguity must be checked on every
mapping, including 4-KiB-mode transitions; a 4-KiB PTE mode alone does not prove
physical scattering. No mandatory fail-instead-of-fallback mechanism is proven.

## Migration options and budgets

**Preferred end state, conditional on backing/coherence:** ordinary BOs in a
system-memory/aperture path, small native local pool for tables/firmware and
consumers requiring physical addressing. Keep the page-table segment until a
replacement can satisfy every table allocation/update callback. Initially retain
segment IDs; migrate one resource class at a time. Do not remove an aperture
from CPU-visible supported sets (EXP1027/1917).

Capacity, physical held bytes, residency and budget must be separate quantities.
Use VidMm's reported DXGI budget/CurrentUsage as the app's working-set limit;
query LOCAL/NON_LOCAL during validation rather than substituting the private
pool's reservation count. UMA system memory is not dedicated VRAM merely because
it is fastest/local. Report DedicatedVideoMemory, DedicatedSystemMemory and
SharedSystemMemory from the actual segment/firmware ownership, with no double
count of RAM used by a driver pool. DedicatedSystemMemory may describe the
remaining reserved portion; shared capacity is not an immediately available
allocation guarantee. Audit both kernel segment groups and any UMD adapter-info
reporting before changing them together. memory_windows.c:3–18 records the
validated grouping (memory segments LOCAL, aperture NON_LOCAL). A new UMA layout
must be measured with DXGI budget queries before relabeling groups; retaining
that grouping during the first functional checkpoint avoids a second variable.

VidMm can evict pageable allocation contents into system backing; Windows can
page eligible backing to disk. Neither GPU-resident pages nor the driver's locked
MDL pool become disk-pageable just by changing budget labels. Eviction must finish
the content transfer and retire references before native backing is released.
Page-in must acquire native backing through a supported failure boundary before
publishing mappings. Do not return a made-up retry from UpdatePageTable, whose
status contract cannot negotiate another physical layout. Unlimited overcommit
and guaranteed paging success cannot both be promised by a finite private pool.

CPU visibility is a correctness contract. A read/write CPU mapping and GPU VA
must refer to the same authoritative bytes. A shadow of scattered pages is viable
only when CPU/GPU ownership changes are observable and synchronized; permanent
upload/readback maps cannot silently become stale snapshots. Aliases must share
backing generation and dirty ownership; copyback must preserve partial-page
bytes and permissions. Keep existing direct-map behavior until this is proven.

**Fallback: demand pool with a larger address reservation.** This already exists
in principle: pool_windows.c:262–303 allocates 2-MiB slabs with 64-KiB contiguous
fallback, and :315–385 maps/unmaps ranges. render_pool_demand.c:261–285 caps the
published capacity against RAM policy, broker capacity and window size. Increasing
capacity requires matched metadata arrays, window address bounds and broker
limits, and worsens retained physical promises unless reservation lifetime also
changes. Never lower ReserveCap below published capacity alone. Do not remove
HighMemory && !LowMemory. A 16-KiB allocation granule could reduce waste, but the
current window/slab ABI is 64 KiB; changing it requires both owners and explicit
permission for G2/m1n1 work. A giant contiguous or large-page pool is less robust
under fragmentation and remains locked RAM; it is not the recommended solution.

**Practical hybrid stages:** move unmapped GPU-only resources first only after a
working scattered-backing path exists; retain a bounded direct native pool for
CPU-visible resources. This improves reclaimability for one class but is not
full unified memory. T7f-aperture-shadow.md is the starting coherence study, not
an already implemented fallback. For true flexibility the unresolved prerequisite
is a documented failure-safe admission/lifetime mechanism or a correct backing
bridge for persistently mapped CPU allocations. Do not hide it with priorities,
trace-specific exceptions or forced global residency.

After the pageable path is proven, ordinary BOs can stop taking allocation-life
AdmissionMemoryPoolReserve promises. The pool window, slab allocator, reserve
floor and private copy indirection may then be removed for those classes, but
remain for native-only consumers until migrated. Firmware/table/scanout storage
and their teardown cannot be deleted as incidental cleanup.

## DMA, DART, hypervisor and Linux differences

Asahi's shmem objects are pageable Linux objects, DMA-mapped into an SG list;
A/mmu.rs:1200 uses dma_address, not an assumption that CPU PA equals device PA.
The PAGE_SIZE_16KB dependency removes the Windows subpage mismatch. This does
not mean Linux has unbounded RAM or that all pinned/in-flight objects can swap.

For Windows each resident PFN run needs a live owner and broker authorization,
translation from guest IPA to permitted host PA, and matching CPU cache type.
The synthetic pool IPA window is an alias, not new physical RAM. Its stage-2
mapping must not outlive the MDL; TLB invalidation and GPU retirement must precede
recycling. UAT and DART are distinct translation/authorization stages; a DART
mapping cannot be assumed to join four arbitrary 4-KiB pages into a native UAT
page. Inspect the exact live m1n1/DART implementation before proposing such a path.
Partial native groups must not expose neighboring allocations. Reset, StopDevice,
suspend and process teardown must drain aliases/references before freeing backing.
Mu ACPI reservations must continue excluding firmware storage from ordinary RAM.
No assisted or standalone launch contract is changed by this branch.

## Implementation steps, tests and stop gates

1. **Delivered accounting prerequisite.** In render_pool_slab.c, reject Count
   exceeding ULLONG_MAX - Reserved before any mutation/callback. Increment
   ReserveFailures only; preserve totals, high water, held pages and floor count.
   Existing C regression harness exercises one live promise plus max request,
   low-memory refusal, reclaim, later take, exact maximum, zero increment and
   one-past-maximum. RED was the failed rejection assertion; GREEN uses actual
   production code with ASan/UBSan. This is arithmetic safety, not OOM relief.
2. **Contract gate before a placement change.** Integration owner supplies exact
   initialized m1n1/Mu sources and generated ACPI matching the accepted launch.
   Establish a supported acquire/release boundary for pageable native backing
   and a CPU coherence scheme. If neither exists, stop: no caps-only experiment.
   Required output is an owner/lifetime state machine, not another receipt patch.
3. **One-class implementation.** Add the chosen bridge to gpuva_g3_paging_windows.c
   and its backing owner, preserving logical 4-KiB mappings, allocation offsets,
   rights, aliases and paging status rules. RED tests: scattered quartet, partial
   update/unmap, 4K/64K conversion, transfer hints in both directions, allocation
   and broker failure, concurrent CPU alias, reset/teardown. Then implement and
   run real paging/graph/broker tests before enabling any placement change.
4. **Reclaim and budgets.** Only after step 3, change allocation_windows.c promises
   for the migrated class and the matching segment budget description. Tests
   must prove eviction returns actual RAM while the resource remains alive,
   rematerialization restores bytes, low memory fails at an allowed boundary,
   and no in-flight or CPU-mapped backing is reclaimed. Do not rewrite descriptor
   capacity during a running adapter without a documented resize contract.
5. **Remove old paths by class** only after the functional/pressure checkpoints
   below. Run existing direct-map, native coverage, pool demand/slab/headroom,
   paging ownership/status, caps and full baseline tests. WDK/native compilation
   is required externally; host C replay cannot replace it.

## Future experiments (integration owner only; none run or preregistered here)

A new run requires exact commits/diffs, package and manifest SHA-256, recorded
known-good visible recovery, and the current control-plane preflight. Keep
firmware, m1n1, caps, signer and all unrelated switches constant within each
comparison. No experiment is justified solely by the arithmetic guard.

**WHY THIS HYPOTHESIS:** RC8 proves scattered CPU-visible backing; current caps
already allow 64-KiB pages; EXP1940 and the slab source establish that allocation
promises retain RAM. Therefore a coherent backing/lifetime change, not another
cap toggle, is the discriminating variable.

A. After step 3, one test allocation in the migrated class: CPU writes, GPU
reads/writes, CPU verifies; evict/remake resident, verify again; include alias
and subpage updates. Require actual scattered-placement evidence plus correct
native coverage/content, not just Code0 or MakeResident success. Stop before
submission if coverage is absent. Record backing generation, PFNs, held bytes,
copy/fence completion and both directions of transfer.

B. Same implementation, controlled pressure: many still-live idle allocations
must release physical RAM after eviction; grow/remake without stale content or
unauthorized paging failure. Sample DXGI budgets, OS available/commit memory and
pool held/reserved/committed separately. Gate on DWM survival and no resets,
faults or unexplained removal. Collect first-failure ETL before ring overwrite.

C. Only after A/B, GravityMark D3D12 2k then 10k, plus C75c direct-map regression.
Success requires actual rendered frames, content correctness, completed workload
and DWM stability. An increased budget or process survival alone is not success.
On any failure collect evidence first, then exact-package ordinary GPU-visible
cleanup. Emergency hidden recovery only under its existing failure conditions.

## WHAT IS STILL UNKNOWN

The general mandatory system-page allocation contract is unproven; physical
backing under future OS versions cannot be inferred from these flags. The first
GravityMark removal site is not settled by rc9. A transparent persistently mapped
CPU shadow, reclaim-safe admission boundary, exact live hypervisor/DART/ACPI
contract, sustainable minimum native pool and correct resulting DXGI accounting
remain implementation gates. No WDK build, package, hardware verdict or performance
claim accompanies this design. The arithmetic fix does not resolve these gates.
