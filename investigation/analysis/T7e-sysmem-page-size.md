# T7e: system backing page size is not a mandatory 64 KiB contract

2026-10-23. Offline analysis at base `d46cbae2b488be2b3df4d3e599d8b857468cd2b8`,
branch `codex/t7e-sysmem-page-size`.

## Decision

There is **no justified small caps/flags fix** for the observed scattered system
backing in the inspected WDDM contract. Keep existing 64 KiB leaf support,
unsupported large-page caps clear, native-group validation, CPU-visible direct
allocation and idle SystemDirect trimming. Do not enable large-page caps, raise
all priorities, or fail UpdatePageTable to request new backing.

This branch adds an executable caps regression and this note, with **no production
behavior change**. GravityMark remains unfixed. The task makes implementation
conditional on finding an offline-testable documented change; that condition is
not met. Correctness needs a backing ownership/coherence solution beyond these
flags, as identified in T7d. Protected C75c behavior remains intact.

## Source evidence and ownership

Paths/line numbers below are relative to this worktree at the base above:

| Source | Observed contract |
| --- | --- |
| `drivers/apple-agx/render-admission/AppleAgxRenderAdmission.inf:89`, `:93` | Registry opt-in already writes Supports64KBPages=1. |
| `drivers/apple-agx/render-admission/src/lifecycle.c:784`, `:806`, `:808`, `:817` | GpuMmu query zeroes output and advertises system 64 KiB support and its leaf-table size. |
| `drivers/apple-agx/shared/include/apple_agx_gpuva_g3_caps.h:20`, `:85` | Three logical levels, 13/11/3 index bits; system 64 KiB support in every enabled profile. |
| `drivers/apple-agx/render-admission/src/allocation_windows.c:467`, `:519`, `:531` | Admitted Alignment contract, pool reservation, pool preferred plus aperture fallback for CPU-visible class BOs. |
| Same file `:541`, `:543`, `:554`, `:556`, `:557` | EvictionSegmentSet=0, zeroed WDDM2 flags, class0-only physical access, normal priority, zero Flags2. |
| `drivers/apple-agx/render-admission/src/memory_windows.c:22`, `:41`, `:56` | Segment4/5 declarations; system slab remains 4 KiB in the experimental segment5 profile. |
| `drivers/apple-agx/shared/src/apple_agx_physical_topology.c:81` | Pool page size and CPU visibility; pool owns backing, unlike aperture. |
| `drivers/apple-agx/render-admission/src/gpuva_g3_paging_windows.c:257`, `:567` | Parent pointer validation and explicit refusal of LargePage leaf entries. |
| Same file `:520`, `:593`, `:642`, `:738`, `:780` | 64 KiB expansion, physical/logical provenance, native-group validation, logical shadow preservation, receipt flag packing. |
| Same file `:1260`, `:1267` | Invalid paging-status bugchecks, including ALLOCATION_BUSY from UpdatePageTable in EXP1320. |
| `drivers/apple-agx/render-admission/umd/src/umd_gpuva_windows.c:258`, `:293`, `:303`, `:338` | Trim reservation under lock, EvictOnlyIfNecessary, residency request/priority loop. |
| `drivers/apple-agx/render-admission/umd/include/umd_residency_trim.h:25` | Submission/source holds and mapped SystemDirect slots are excluded from trim. |
| `.local/reference/wdk26100/d3dkmddi.h:2055`, `:2081`, `:2113` | Update mode, flags and caps/version guards. |
| Same header `:2558`, `:3799`, `:3850`, `:3910`, `:11082`, `:11104` | Segment flags, allocation flags/Flags2, allocation info, page-size enum and segment5. |
| `.local/reference/wdk26100/d3dukmdt.h:305`, `:316`, `:419`, `:1459`, `:1475` | Leaf modes, PTE/address units, UMD allocation info2, MakeResident flags/request. |
| `.local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi/pgtable.rs:39`, `mmu.rs:1221` | Native UAT_PGBIT=14 and aligned mapping contract. |
| `.local/reference/mesa/src/asahi/lib/agx_bo.c:355`, `:363`, `:379` | BO size/alignment rounding before the OS backend allocates storage. |

Machine evidence is the supplied T7b EXP1915 decode, T7d report and
`investigation/analysis/M2-vidmm-64k-decision.md`. No live ADT, registers, routes
or memory map were queried. Both submodules are uninitialized here:
`m1n1_windows` gitlink `21c3648d32423def46086e258e0995c737dc9284`,
`mu` gitlink `361a2134b2f742f331f8d75ae5f60ad4f7f413f1`. Current firmware and
ACPI were not reverified. No firmware or launch change is proposed.

VidMm owns backing selection, placement and logical GPUVA; UMD owns residency
references and CPU/GPU synchronization; KMD owns native translation, its pool
backing and fail-closed submission. m1n1 retains broker/stage-2 ownership and
Mu retains firmware exposure. Initialization, interrupts, DMA authorization,
power and recovery remain with their existing owners. Linux's native-page
allocation contract is not a guarantee about Windows PFNs. No external code
was copied into production; tests extract local WDK types into temporary files.

## 1. Fallback and a mandatory 64 KiB request

Separate physical backing, logical PTE format and native GPU leaf size.
Sixteen 4 KiB PTEs can describe contiguous 64 KiB backing; a 64 KiB-aligned VA
can describe scattered backing.

Microsoft's [64 KiB page-table contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/support-for-64kb-pages)
selects leaf mode using allocation alignment/size and segment support. In single
PTE mode the whole leaf-table VA range must qualify. An incompatible mapping
can convert the table to 4 KiB PTEs; contexts are paused, leaves and parent mode
updated, then contexts resume. The described model has no reverse conversion.
That is a PTE-format rule, not a universal physical-backing guarantee. The
[GpuMmu model](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpummu-model)
exposes logical 4 KiB granularity and leaves hardware representation to the KMD.

| Candidate | Evidence and limits |
| --- | --- |
| Alignment/size | EXP1915 VA 0x1192ad0000 and size 0x08100000 are both 64 KiB multiples. These alone cannot explain the refusal. Other mappings in the same table range can affect mode. |
| Flags and segment set | Strongest eligibility explanation: M2's image-specific decode (`:31`–`:50`) excludes ordinary CpuVisible allocations with a CPU-visible non-aperture segment in their set unless supplied backing changes that branch. Current class BOs have that mixed pool/aperture shape. Applying this decode to EXP1915 is an inference, not a captured branch trace. |
| Fragmentation/pressure | Could limit contiguous backing; the public sources inspected do not specify the exact allocator fallback algorithm or promise failure instead of scattered pages. Pressure is not proof of fragmentation. |
| Initial versus eviction | Both can affect residency/backing. EXP1915's last tracked update is InitialUpdate=1, NotifyEviction=0. It does not prove eviction caused the first system mapping, nor is a last-event receipt a lifetime trace. |
| AllowNonAlignedLargePageAddress | Relaxes the physical-address alignment of non-leaf LargePage entries relative to table coverage. It neither mandates 64 KiB backing nor joins scattered PFNs. |

M2's earlier missing-registry explanation (`:57`) is historical; the current INF
already opts in. Its addendum (`:92`) reports scattered backing after opt-in.
EXP1915's four valid writable PFNs are nonadjacent and the first is not 16 KiB
aligned. Those addresses establish unrepresentability, unlike page-mode flags.

No inspected public KMD allocation flag mandates 64 KiB system backing with
fail-on-fallback behavior. The pinned header marks MinimumPageSize and
RecommendedPageSize **input**, sharing storage with Alignment. The
[allocation-info documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_allocationinfo)
does not specify a KMD output opt-in using these fields. The normal project
profile is WDDM 3.0 (`AppleAgxRenderAdmission.vcxproj:24`–`:27`); page-hint/segment5
trials belong to a separate WDDM 3.2 profile. EXP836's rejected page-hint output
is recorded at `allocation_windows.c:467`. Writing DXGK_PAGESIZE_64KB (4) over
the ordinary Alignment field is not a documented mandatory-page request.

[Segment flags](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_segmentflags)
define Use64KBPages as support and require suitable allocation alignment; an
aperture still maps backing it does not own. ReservedSysMem is reserved. M2's
forcing per-call bit has no identified public API in that note: do not invent
one, write reserved bits, or change global registry policy based on it.

## 2. SysMemLargePageSupported is a different capability

The pinned WDK exposes SysMemLargePageSupported at WDDM 2.9. Microsoft's
[caps reference](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_gpummucaps)
describes large-page support, while LargePageSupported covers non-leaf entries
and AllowNonAlignedLargePageAddress relaxes their address alignment. It does not
promise mandatory 2 MiB physical allocation. DXGK_PAGESIZE_2MB in segment5 is a
separate interface; its existence does not supply that guarantee.

Current level-1 coverage is 8192 * 4096 = **32 MiB**, and level-2 coverage is
64 GiB. A cap toggle cannot turn that geometry into a 2 MiB block mapping.
DXGK_PTE.PageAddress remains expressed in 4 KiB units. Parents accept table
pointers, not LargePage blocks; leaves explicitly reject LargePage. An extension
needs geometry, physical alignment/coverage, block-to-native translation,
partial updates/splitting, ownership, references, unmap/replacement and TLB/job
ordering, with tests before advertising support. Physically contiguous larger
backing could also be split into ordinary native leaves; physical size and
LargePage PTE format are distinct. The sparse SysMemLargePageSupported docs do
not establish whether this OS can use that cap without non-leaf LargePage
support. That missing contract is not permission to enable it experimentally.

[Allocation flags](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_allocationinfoflags_wddm2_0)
describe DisableLargePageMapping as suppression of block mappings; it is already
clear and clearing it does not require large physical backing. PermanentSysMem,
ExistingSysMem/ExistingKernelSysMem, AccessedPhysically and MapApertureCpuVisible
change storage/addressing contracts, not minimum PFN contiguity. Supplying owned
backing would require a separate CPU/GPU alias and lifetime design.

The online caps syntax also lists PerPtePageSize and
OpportunisticSysMem64KBPageSupported with empty member descriptions. Both are
absent from the pinned WDK struct, where these bits are reserved
(`d3dkmddi.h:2134`). Do not emulate them with raw bit assignments.

## 3. Detection, retry and residency priority

UpdatePageTable supplies PFNs, flags, VA, allocation identity and offset.
AdmissionG3UpdateLeaf already detects complete aligned native quartets with
compatible permissions/segment, preserves logical pages for CPU paging and
invalidates unrepresentable native leaves. G4 refuses access to them. This
identifies the consequence, not VidMm's reason for choosing the PFNs.

[BuildPagingBuffer](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_buildpagingbuffer)
has no documented reallocate-elsewhere status. INSUFFICIENT_DMA_BUFFER requests
command-buffer space, not different PFNs. ALLOCATION_BUSY has an AllocationIsIdle
retry for Transfer, DiscardContent and SpecialLockTransfer, not a placement veto
for UpdatePageTable. EXP1320 records 0x10E/0xB for that return from UpdatePageTable
(`gpuva_g3_paging_windows.c:1267`). The checked wrapper protects the guest and
poisons the process for forbidden failures; do not bypass it. No inspected
paging-operation output renegotiates allocation segment sets or page size.

D3DDDI_ALLOCATIONPRIORITY_MAXIMUM (0xc8000000) is not a pin.
[SetPriorityCb](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/nc-d3dumddi-pfnd3dddi_setprioritycb)
explicitly makes priority a hint which may be ignored, influencing both retention
and placement preference. It cannot guarantee local placement/backing shape.
Changing KMD initial priority alone would also meet normal UMD PriorityList
values on later MakeResident calls (`umd_gpuva_windows.c:384`, `:579`, `:942`,
`:1275`), so that is not a durable policy by itself.

The lifetime tool is residency references, balanced MakeResident/Evict calls
and paging-fence ordering: [residency overview](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/residency-overview),
[MakeResidentCb](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/nc-d3dumddi-pfnd3dddi_makeresidentcb).
UMD must hold references while commands use allocations. This ensures residency
for execution, not immutable segment addresses or a physical layout. Current
trim already excludes held/mapped slots. CantTrimFurther/MustSucceed concern
budgets/error handling, not page size. Wholesale pinning or disabling idle
SystemDirect trim is unsupported by EXP1915's initial mapping and would weaken
protected C75c behavior.

## Verification and next checkpoint

`tests/test_g4_sysmem_page_caps.py` executes the real GPUMMUCAPS query case and
model using pinned WDK declarations, WDDM 3.0/3.2 and local profiles 0/16/64. It
starts with dirty output and verifies supported 64 KiB leaves plus absent
unimplemented large/dual modes/reserved bits. Four temporary mutations enabling
the unsupported caps each fail their assertion. This guards an existing
invariant; it is **not** RED/GREEN evidence of a production fix. Existing native
leaf and paging-status replays cover scattered backing and the retry boundary.
Exact commands/results are in the handoff REPORT.md.

No hardware run is justified by this test-only branch. Do not repeat the old
aperture-only/64 KiB opt-in experiment: EXP1244 already demonstrated scattered
backing after it. If the integration owner needs the precise eligibility cause,
collect first-placement history and actual allocation output/registry state in
an independently justified run; the existing last-event receipt cannot decide
that branch.

The smallest hardware checkpoint **after a real backing/coherence implementation
and offline tests exist** is one writable CPU-visible General BO deliberately
reaching the system-fallback boundary: CPU write -> GPU read/write -> CPU
readback, then release/remake residency and repeat, including an alias and a
partial-page access. Capture logical/native backing identity, PFNs and fences;
stop before dispatch of an unrepresentable group. Choose sizes from the page
contract, never EXP1915's exact 129 MiB identity. This is a conditional future
checkpoint, not a preregistered experiment or a claim that such a fix exists.

WHY THIS HYPOTHESIS: (1) EXP1915 has valid writable scattered PFNs that cannot
form a native leaf; (2) the existing 64 KiB opt-in has not prevented that shape;
(3) direct CPU and GPU writes require one coherent backing identity. The
checkpoint must prove that identity across placement changes before another
GravityMark run can establish application behavior.

Claude owns preregistration, exact package/hash, known-good recovery preservation
and evidence-first ordinary GPU-visible rollback. KMD changes need the existing
reboot contract. No assisted/standalone launch change is proposed; this branch
produces no installable artifact and issues no hardware commands.

## WHAT IS STILL UNKNOWN

- Which internal eligibility/backing branch EXP1915 actually took; the supplied
  M2 disassembly is image-specific and was not independently rerun here.
- Whether supplied backing, the mixed-segment predicate or fragmentation caused
  its first scattered mapping; public sources inspected provide no exact fallback
  algorithm or mandatory public 64 KiB flag for this path.
- A fully specified newer interface beyond the pinned WDK contract; names and
  empty online descriptions do not establish a supported implementation.
- A performant backing/coherence design preserving direct CPU access, writable
  aliases, persistent maps, eviction, fences, reset and budget semantics.
- Current firmware/ACPI and hardware behavior of any future design.
