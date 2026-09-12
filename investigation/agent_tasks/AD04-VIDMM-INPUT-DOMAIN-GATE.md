# VidMm input-domain gate — current-target NO / source review only

Input e8e276a6871c54c23657e1d1c0fd0ef4a00d0af6. Current driver explicitly
registers WDDM3.0 (driver.c15; lifecycle.c390/442). Header baseline is WDK26100.
No implementation/build/hardware/feature query/registry override was performed.
No claim about live Air OS feature enablement; it was not contacted.

## Classic GpuMmu: exact distinction

- DXGK_PAGE_TABLE_LEVEL_DESC says VidMm describes leaf entries on a4KiB basis.
  PageTableSizeInBytes/alignment describe table storage, not a negotiated16KiB
  minimum mapping/update granularity.
- Classic DXGK_PTE_PAGE_SIZE in pinned d3dukmdt.h305–309 has only4KB and64KB.
  Use64KBPages/dual-PTE handling do not provide16KB-only input negotiation.
- UpdatePageTable requires sequential allocation offsets but explicitly does
  not promise physical contiguity. The KMD must implement valid4KiB operations;
  a contiguous same-protection16KiB input group cannot be assumed globally.
-64KiB page support includes demotion to4KiB tables, e.g. on system-memory
  placement. Forcing64KiB allocation sizes is not a proof that this path disappears.
- Important: UpdatePageTable DOES explicitly discuss16KiB hardware pages and
  projecting the logical entries into larger GPU pages. Do not rewrite that
  source as "Windows cannot ever use16KiB GPU hardware." It is not an explicit
  guarantee that arbitrary legal4KiB backing/protection updates are excluded
  for our proposed table/segment configuration.

Verdict wording: NO to an officially established16KiB-only input-domain contract
for this classic migration, not a universal impossibility claim about all larger
hardware-page implementations. Valid independent4KiB requests cannot be rejected,
silently rounded, overmapped, assigned different permissions or regrouped.

Primary Microsoft sources:

- https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_page_table_level_desc
- https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_buildpagingbuffer_updatepagetable
- https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dukmdt/ne-d3dukmdt-_dxgk_pte_page_size
- https://learn.microsoft.com/en-us/windows-hardware/drivers/display/support-for-64kb-pages

## New page-based family: present does not mean enabled

Pinned shared/d3dkmddi.h contains DXGK_PAGESIZE_16KB=2 at11086. This declaration
is inside the WDDM3_2 guarded block10439–11225. Its in-header consumer is
DXGK_SEGMENTDESCRIPTOR5.SlabSize at11121. Slab size must not be conflated with
classic per-process GPUVA leaf/input granularity.

Related declarations include QUERYSEGMENTCOUNT/QUERYSEGMENT5, QUERYMMUCOUNT/
QUERYMMUS, MMUDESCRIPTOR, QUERYMMUSOUT.DisplayMmuId and page-based paging ops
MAPMMU/UNMAPMMU/NOTIFY_RESIDENCY2/NOTIFY_ALLOCATION. New operations and union
members are guarded by WDDM3_2. d3dukmdt.h2161/2215 contains driver feature32,
DXGK_FEATURE_PAGE_BASED_MEMORY_MANAGER.

The public DXGK_PAGESIZE documentation explicitly carries a PRERELEASE warning
and references "Page-based memory manager" without supplying a complete usable
contract there. Other public structure pages expose names but not a full16KiB
per-process GPUVA/residency replacement specification. DisplayMmuId shows a
display-facing role; it is not evidence that this is merely an unrelated CPU
allocator, nor proof that it replaces classic process GPUVA semantics.

The feature-query mechanism/feature-ID documentation has minimum Windows11
24H2/WDDM3.2. This establishes an interface-generation floor, NOT the exact first
shipping/enabled page-based16KiB production configuration. No publicly verified
production minimum or complete applicable contract was established by this pass.
Current WDDM3.0 driver does not implement/negotiate this family. No implicit
version upgrade, feature override or test enabling is authorized.

Sources:

- https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ne-d3dkmddi-dxgk_pagesize
- https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dukmdt/ne-d3dukmdt-dxgk_feature_id
- https://learn.microsoft.com/en-us/windows-hardware/drivers/display/querying-wddm-feature-support-and-enablement

Header identities independently checked on FRYZZING:

- d3dkmddi.h SHA256 c13cecb0ce73e7bbdb6bec8586d05eea31932a8c532bec49b3dae4a03054770e
- d3dukmdt.h SHA256 d1c43d619589c2eb3f47877bba6d48735c7f26644fed44a5bb682f9e591eb33f

## Decision

FINAL_GPUVA_MIGRATION_VERDICT=NO for the current approved target and confirmed
production contract. Do not turn missing production guarantees into another
CONDITIONAL implementation campaign. Preserve previous semantic-model results
as scoped evidence; they never established the VidMm input domain.

This is a current-target engineering no-go, not a claim that newer/private/
future Windows page-based contracts can never support AGX. Reconsider only with
an exact officially supported applicable contract and a separately approved
target change. Header enum availability is insufficient.

## Return to physical/patch-list: minimal adapter assessment, no code

Keep Windows device/context ownership, Render/Patch/SubmitCommand, allocation
lists, existing independent residency checks, retained-root broker and exact
completion/fence path. Preserve compiler and physical AGX hardware proofs.

Smallest falsifiable native-Asahi adapter scope: CPU-generated direct draw,
existing native VS/FS compilation, one target and fully enumerated dependency
graph. This is a workload restriction for offline validation, not a new feature
level/capability promise or a complete desktop driver.

1. At BO/pool/suballocation creation retain allocation token, generation, owner,
   class, size and subrange provenance. CPU mapping stays separate. Any native
   address-shaped placeholder is an internal relocation symbol, NOT a Windows
   GPUVA or a physical address. Numeric compatibility with native packing must
   be proven before choosing this representation.
2. Capture typed references where native Asahi emits pointers: USC shader/base
   offsets and uniform buffers; texture/PBE/descriptor addresses; PPP/VDM links.
   Record target allocation/addend and field encoding, not searches for matching
   integer values. agx_state.c pipeline construction is an initial concrete site.
3. Serialize bounded dependencies and relocation records through the existing
   Windows command/reference machinery. KMD validates owner/generation/range,
   residency/placement and resolves actual AGX addresses before dispatch.
4. Patch an immutable submission-specific command/object image with exact current
   placement. Do not mutate active/in-flight cached descriptors or assume DdiPatch
   can arbitrarily write every UMD allocation. Nested-object materialization and
   lifetime are explicit adapter work, not a free property of physical mode.
5. Hold backing/image/dependency lifetime through the exact fence. On movement,
   regenerate/repatch all address-bearing data; do not cache a stale physical bind.

Reuse the seven existing relocation kinds and current dynamic-overlay validation
only where their native field contracts match. Current template207 relocations
are not the general native graph. GPU-generated indirect data, arbitrary SSBO/
bindless address use and other untracked pointer creation remain outside the
initial proof; they cannot be advertised as supported merely because direct draw
works. Required feature-level completeness remains an eventual obligation.

Offline acceptance for this adapter: emit the same draw under two distinct valid
placements; all resolved references follow the corresponding allocations while
non-address bytes and native semantics remain unchanged; stale/foreign/range or
untracked references reject; no user PA leakage or residency inferred from BO.
No implementation was performed during this gate/assessment.
