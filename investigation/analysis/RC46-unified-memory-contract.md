# RC46: unified-memory contract and corrected EXP1971 evidence

2026-10-26. Driver base `58afbe24761cb2b0a96e8d828ec9a870b4033b09`.
Offline investigation; no new hardware verdict. **The requested production
unified-memory implementation is not delivered.** The concrete implementation
in this branch repairs the benchmark runner's ignored resolution parameters.
It is not a memory-capacity fix or a runnable unified-memory driver candidate.

## The evidence changes the proposed fix

The actual expanded-RAM native segment is **3136 MiB**, already greater than
2688 MiB. EXP1971's 43 native reservation refusals are all Windows headroom
vetoes; the recorded last failure is below segment capacity. The current broker
already accepts eligible raw guest RAM outside its synthetic pool window.
Increasing a window, changing a cap, or adding another raw-page hypercall would
therefore not address the demonstrated allocation failure.

Evidence paths below are relative to `/Volumes/pwdev/public_windows/.local/`.
`D` means `drivers/apple-agx/` in this repository. `W` means
`.local/reference/wdk26100/` in this worktree. `H` means the exact exported
m1n1 commit in `handoff/codex/rc46/source-evidence/m1n1-075e3980/`.

| Saved observation | Value / meaning |
| --- | --- |
| `experiments/EXP1971-escape-batch/evidence/EXP1971/kmd-receipts-probe-end.txt:159` | v8, 360-byte pool receipt: Windows RAM 7,218,794,496 B; segment 3,288,334,336 B = 3136 MiB; broker coverage 4,831,838,208 B = 4608 MiB. |
| Same receipt; recovery `evidence/rec1971h/kmd-receipts-pre-disable.txt:159` | ReserveFailures=FloorRefusals=43; NewSlabFailures=CommitFailures=0; LastWindowResult=0. The two pool records are identical. |
| Persisted last refusal within that receipt | Request 64 MiB; old reserved=committed=3036.4375 MiB; held=3042 MiB. The new promise would be 3100.4375 MiB, below 3136 MiB. HighMemory=0/LowMemory=0 are post-failure samples; the counter identifies the actual floor decision. |
| Later staged totals, not failure-time state | Committed 91.375 MiB; held 190 MiB; reserved 189.9375 MiB. Peak committed 3128.75 MiB. Peak live reservations 3351.3125 MiB may exceed capacity because the backing floor is min(live, cap). |
| Slab acquisition totals | 2776 contiguous 2 MiB slabs; 246 successful fallbacks to 32 x 64 KiB chunks. No recorded physical slab allocation failure. Whole-resource physical contiguity is already unnecessary. |
| `gm1971a-umd.log:6604` and adjacent refusal records | First refusal request 129 MiB; canonical 2,974,859,264 B; private staging 713,752,576 B; process private commit 4,118,683,648 B; physical available 914,432,000 B. Staging is included in process commit; these numbers must not be added as disjoint categories. |
| `gm1971b-umd.log:12080` | A different boundary: LeafAbsent for handle 0x40000540, VA 0x20000, length 64 KiB. The native-coverage proof maps this to E_OUTOFMEMORY. There is no arm-b AllocateCb refusal snapshot. |
| `gm1971a-out.txt:19`, `:32`, `:37`; corresponding arm-b output | Both runs create 200,000 asteroids at **1280x720**, not FHD. The saved runner ignored Width/Height and hardcoded 720p. |

The receipt layout is `D/render-admission/include/render_pool_demand.h:99-122`;
its producer is `src/pool_windows.c:443-472`. The size calculation at
`src/render_pool_demand.c:261-284` reproduces 3136 MiB from the actual RAM total:
RAM/2 minus 256 MiB, rounded down to 64 MiB. The 2688 MiB headline described the
legacy RAM profile. The complete decodes, source hashes and scripts are in
`handoff/codex/rc46/failure-evidence.md`, `failure-kmd.json`, `failure-umd-a.json`
and `source-evidence/memory-receipts-decoded.json`. The largest free virtual
run and the full 200k scene requirement are not in these receipts; neither is
invented here. These results do not rule out fragmentation in an unobserved
later allocation, but the recorded vetoes never reach the slab allocator.

EXP1955 (`EXPERIMENTS.md:63995`) demonstrated headroom refusal with the legacy
full pool. EXP1957 (`:64019`) measured physical/commit pressure and long paging
waits. EXP1959 (`:64050`) demonstrated that persistent CPU pool maps exhaust
finite native residency. EXP1961 (`:64075`) validated expanded guest RAM;
EXP1962 (`:64096`, correction `:64098`) validated the larger full-pool operating
point. Those are preserved historical results; no ledger was edited here.

## Hardware, hypervisor and firmware contract

Source order: saved launch/receipts, current local Asahi, exact m1n1 export,
exact Mu export, pinned Windows headers and saved Microsoft documentation notes,
then comparison with the assisted launch/recovery contracts. No live state was
queried. No external implementation was copied into driver code.

The Asahi reference at commit `77cb8f24c2381a8abb7272d7bbdec548d6426a8a`
requires PAGE_SIZE_16KB (`drivers/gpu/drm/asahi/Kconfig:18`), creates GEM shmem
objects (`gem.rs:57-58`), obtains/pins ordinary DMA SG pages (`mmu.rs:1191-1216`),
checks native alignment (`mmu.rs:493-538`) and emits 16 KiB UAT leaves
(`pgtable.rs:39-60`, `:486-519`). Linux's 16 KiB kernel pages provide the
minimum physical unit. Windows' four unrelated 4 KiB PFNs do not provide it.
The GPU uses host physical addresses in UAT leaves; DCP's DART is a separate
translation domain. No 4 KiB UAT mode was found in the inspected primary code.

m1n1 commit `075e39804b17e8f1fa653e9cac8804c7a619ff80` was exported from the
allowed rc30 handoff object database, without accessing another worktree.
`H/manifest.json` pins the exported files. Saved EXP1962 `full.log:15,100,128`
establishes ordinary guest RAM `[0x818000000,0x9df708000)` and the expanded
proxy-heap bound. Its `:155` establishes the low alias; `:102-103` records the
4 GiB synthetic pool window and 294912 native backing slots.

There is no globally correct IPA-to-PA offset:

| Address domain | Saved mapping |
| --- | --- |
| Upper ordinary guest RAM | Normally identity mapped. |
| Guest IPA `[0x100000,0x40000000)` | Host PA = IPA + 0x8a0000000. |
| Synthetic pool at 0xd00000000 | Each native page maps to independently allocated backing. |

Production translation must walk stage-2 (`H/src/hv_vm.c:589-599`), as the
existing broker does. `hv_agx_retained_platform.c:50-68` checks the full 16 KiB
leaf; `hv_agx_retained_backing.c:17-39,42-62` applies validated launch RAM and
reserved-range exclusions. Generic REGISTER_BACKING has no pool-window-only
test (`hv_agx_gpuva_v5.c:459-521`). Registration/publishing binds IPA, translated
PA, process, generation and permissions; live jobs prevent unsafe updates,
publication includes visibility/TLBI, and live leaves prevent revoke
(`:300-322`, `:540-572`, `:674-697`). The KMD must retain the real MDL until
GPU retirement, leaf invalidation, acknowledged TLBI and grant revoke.

The independent broker-record ceiling is 4.5 GiB
(`H/src/hv_agx_power_mmio.c:115-133`). Mapping more distinct native pages
concurrently would require capacity sized from validated RAM or a reviewed
range-grant implementation. This is metadata, not a GPU backing reserve; an
increase does not allocate resource RAM or resolve Windows paging. It was not
the observed EXP1971 refusal. A future raw-page path need not enlarge the CPU
pool window at all.

The current Wom1SysmemGpuMap opt-in expects rc26 commands 17/18, absent from
075e3980's command range. That absence is separate from generic raw-page access.
The optional strict extension must be made alias-aware before claiming all
Windows RAM: its original IPA-range policy excludes the approximately 1 GiB low
alias. Do not hardcode IPA >= 0x818000000 or pass Windows PFNs as host PAs.

Mu commit `361a2134b2f742f331f8d75ae5f60ad4f7f413f1` source exports in
`handoff/codex/rc30/source-evidence/Mu-361a2134/` show bootargs RAM publication
(`AdtParser.c:25-36`), RAM HOB construction/FD splitting
(`MemoryInitPeiLib.c:252-340`), removal of high alias backing and insertion of
low IPA RAM (`:440-459`), and preservation of the AGX reserve (`:510-514`).
Existing APPL0002 ACPI exposes the broker's actual reserve. No Mu change is
required merely to map correctly allocated Windows RAM. The saved assisted
full-owner launch and legacy recovery are the evidence; a new standalone
launch certification is outside this offline task.

## Windows contract and the missing binding

The pinned GpuMmu interface consumes logical 4 KiB pages. System/aperture
placement may use scattered PFNs. 64 KiB leaf support and large-page support
are distinct capabilities; neither is a mandatory allocation-page contract.
Current `D/render-admission/src/lifecycle.c:844-856` advertises implemented
64 KiB support and leaves unimplemented large-page modes clear. Logical/native
projection rejects incomplete, nonadjacent or mismatched 16 KiB groups
(`gpuva_g3_paging_windows.c:721-744`). This is a hardware requirement.

| Pinned primary interface | What it establishes / does not establish |
| --- | --- |
| `W/d3dkmddi.h:2113-2161` | GpuMmu caps. No mandatory 16 KiB system-backing allocator input. |
| `:3918-3923` | Minimum/RecommendedPageSize are input fields over Alignment; not a proven output mechanism to force contiguous system pages. |
| `:2565-2589`, `:2720-2749` | Segment ownership, CPU visibility, size and aperture commit limit. No dynamically resizable segment or allocator provider binding is supplied by the descriptor. |
| `:4951-4958`, `:5043-5051` | MapAperture takes an incoming MDL/ADL; it does not return replacement backing. |
| `:929-945`, `:3856` | SetAllocationBackingStore receives a const pointer notification; ShareBackingStoreWithKmd requests visibility, not ownership replacement. |
| `:9536-9546`, `:9992-10044`, `:10108-10124` | MDL/physical-memory-object/ADL allocation. No ordinary allocation/residency transaction binding that makes these chunks pageable. |
| `W/d3dukmdt.h:419-426` | Existing system backing is an input pointer. Importing permanently pinned pages does not make them reclaimable while live. |
| `W/d3dkmddi.h:3857-3862`, `:4868-4878` | WDDM3.2 eviction notification is not available by setting reserved bits in the current WDDM3.0 contract; notification alone is not re-admission. |

`winsdk26100/km/wdm.h:29767-29775,29832-29838` supplies
MmAllocatePagesForMdlEx and REQUIRE_CONTIGUOUS_CHUNKS. The existing allocator
already requests compatible chunks and verifies MDL size, alignment and every
PFN (`D/render-admission/src/pool_windows.c:276-290`). It is fallible. Its
existence does not establish how VidMm would own, evict and restore these pages.

One new bounded offline runtime check closes a tempting alternative:
`D3DKMT_CREATEALLOCATIONFLAGS.PhysicallyContiguous` (`W/d3dkmthk.h:1578`).
Cached Windows build 26200 dxgkrnl, SHA-256
`3b3ac442dd1c6cc42ffc7e375aac90dc8625447049ef85a6fcbcc5f4737998d0`, tests that
bit at RVA 0x247434, requires CrossAdapter at 0x2475f8, and returns
STATUS_NOT_SUPPORTED for ordinary allocations (literal at 0x2481c0). The
diagnostic explicitly names the non-cross-adapter case. The hash-pinned script
and disassembly receipt are `handoff/codex/rc46/windows-contiguous-abi.{py,txt}`.
CrossAdapter is a different allocation contract, not permission to bypass this
check; whole-allocation contiguity also is not a bounded native-chunk allocator.
This conclusion is image-specific, not a universal assertion about all Windows.

The relevant Microsoft references are [GpuMmu model](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpummu-model),
[64 KiB pages](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/support-for-64kb-pages),
[segment flags](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_segmentflags),
[graphics-memory accounting](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/calculating-graphics-memory),
[BuildPagingBuffer](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_buildpagingbuffer),
and [residency](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/residency-overview).
They are identified from stored project studies (`M2-f3-demand-pool.md`,
`T7e-sysmem-page-size.md`); no fresh web fetch is claimed in this offline task.
In particular the stored accounting formula limits driver-populated system
segments to graphics-available RAM, about half RAM on this machine. Giving the
GPU access to any eligible guest page is different from promising simultaneous
use of every byte while Windows and applications also need RAM.

## Architecture, owners and falsifiable implementation gates

The target remains dynamically allocated native chunks from eligible Windows
RAM, with truthful VidMm accounting, coherent CPU mappings and actual return of
RAM when no longer needed. Firmware/RTKit/control storage may retain the proven
small reserve. A second permanent pool, a larger fixed window, or a raw MDL
escape without pageable ownership does not implement that target.

The preferred one-copy design needs an OS backing-provider/page-size contract
covering every resident cycle and persistent CPU map. **That specific contract
has not been established by the inspected ABI.** A secondary design would keep
pageable canonical backing and acquire a separate native cache only at a
fallible preparation boundary before accepted GPU work. It can address page
geometry but introduces copies and a new owner; it is not yet a production UMA
implementation. Existing T13 copies and rc18/rc20 leases are useful components,
not evidence that the complete protocol is correct.

Before implementing the latter, the following transitions must be specified and
tested as actual callers, rather than only a standalone allocator:

1. Create/open admit canonical storage without claiming a private native segment
   can later supply unreserved bytes. Shared handles use one allocation owner and
   generation. All restore/Lock/submission paths must cross the same fallible
   native preparation boundary, including implicit OS paths.
2. Acquire verified native chunks, hold original residency and CPU mappings,
   copy authoritative bytes, register alias-aware grants and publish leaves only
   after paging/content readiness. Partial failure unwinds only new ownership.
3. Hold bytes/permissions/generations through GPU completion. Persistent UPLOAD
   and READBACK pointers must remain coherent; disjoint CPU and GPU writes,
   aliases, partial ranges and queued copyback cannot overwrite newer content.
4. Retire/copy back before releasing fences that authorize CPU reads or reuse;
   invalidate leaves, acknowledge TLBI/revoke, release references, then free
   MDLs. Cancel/reset/process exit/adapter stop must retain uncertain ownership.
5. Evict existing live objects, return their physical pages, deny all subsequent
   native allocations, then restore them. The result must be a supported clean
   refusal **before accepted work**, not an UpdatePageTable failure, missing
   native leaves acknowledged as usable, an infinite refill wait, or 0x10E.

Current private-segment backing remains `min(live, cap) + private` at
`render_pool_slab.c:22-31`. Admission is at `allocation_windows.c:537-540`;
later placement is at `gpuva_g3_paging_windows.c:493-504`. RC28's counterexample
already proves that lowering the retained floor cannot guarantee three serial
restores while allocation remains denied. Keep that promise until an actual
replacement admission protocol covers all callers. No test-derived special
sizes, PFNs, offsets or bind combinations are admissible exceptions.

| Responsibility | Owner / invariant |
| --- | --- |
| Init / exposure | Mu publishes RAM/reservations; m1n1 validates launch bounds; KMD initializes only supported contracts. |
| Pageable storage / budgets / placement | VidMm; the missing native backing-provider or complete re-admission protocol must integrate here. |
| CPU pointer / submission holds | UMD with Windows locks and residency fences; never free mapped or in-flight storage. |
| Native MDLs / cache types / content ordering | KMD; one CPU cache policy per backing, no WC/cached alias substitution. |
| DMA permission / IPA-to-PA / UAT / TLBI | m1n1 broker; preserve process/generation and reserved-PA checks. |
| Interrupts / power / recovery | Existing owners and proven full-owner/recovery profiles remain unchanged. |

## Delivered checkpoint fix and next run

`tools/agx_gravitymark_run.ps1` imports the saved runner into tracked source,
binds positive Width/Height, forwards them in both display modes, rejects unknown
named parameters and records the launch arguments. Existing positional parameters,
1280x720 windowed defaults and app-selected fullscreen size (when neither dimension
is supplied) are retained. `tests/test_d3d12_gravitymark_run.py` catches
the original ignored-parameter/hardcoded-dimension defects. These are source
contract tests; this host has no PowerShell and no Windows execution is claimed.
The integration owner must stage this tracked runner instead of the old
scratchpad copy before using it. The saved scripts and guest were not modified.

No memory-driver candidate or new Mach-O exists, so a unified-memory hardware
run is not justified by these commits. Once the missing contract is implemented,
the smallest checkpoint is an allocation/eviction/restore/content probe, first
including raw upper RAM and the low alias, then a simultaneous native working
set beyond 2688 MiB **and beyond the actual 3136 MiB segment boundary**. Demand
must be derived from the queried capacity, not a trace-specific size. Check
CPU-visible persistent maps, byte correctness, measured physical RAM return,
budget accounting, pressure-denied restore and acknowledged teardown with DWM
alive. Merely exceeding 2688 MiB no longer discriminates the new architecture.

Only then run `-Api d3d12 -Width 1920 -Height 1080 -Asteroids 200000`, verify
the launch JSON **and the app's created/resized dimensions**, require the full
scene to load and the benchmark to finish, and compare with the task's roughly
5500-point macOS reference at matching settings. No score is predicted here.

The integration owner must pin package/profile/artifact hashes and preserve the
known-good full-owner and legacy GPU-visible recovery artifacts. Collect dumps,
receipts and traces before exact-package cleanup; uncertain revoke retains pages.
No launcher, reboot, package install, control-plane query or recovery was run.
