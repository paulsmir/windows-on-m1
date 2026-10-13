# M2-f2: back the GPU memory segment with KMD-owned Windows memory (design, 2026-10-13)

Evidence: EXP1244 (VidMm system memory cannot back 16 KiB UAT pages), EXP1246 (Windows grants the
KMD a 1 GiB physically contiguous write-combined block at StartDevice in 123.5 ms,
`MmAllocatePagesForMdlEx(..., SkipBytes 0, MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS)`).

## Decision: keep segment id 2, change its backing

A third segment would touch ~60 code sites and ~40 tests that hard-code `ADMISSION_MEMORY_LOCAL_SEGMENT
== 2` (inventory below). Instead, the VidMm-visible part of segment 2 (today `LocalAllocationBytes`
0x3b800000 at offset 0 of the 1 GiB m1n1 reserve) moves to the KMD block; the reserve keeps only the
firmware-owned regions (private 64 MiB at 0x3b800000, backend 8 MiB at 0x3f800000) and later shrinks
(`HV_AGX_LOCAL_BYTES`). Segment 2 is then reported `PopulatedFromSystemMemory` (not
`PopulatedByReservedDDRByFirmware`), `CpuTranslatedAddress` = block IPA.

WINDOWS CONTRACT: DXGK_SEGMENTFLAGS.PopulatedFromSystemMemory ("populated from system memory that the
driver allocated during initialization"); DXGK_SEGMENTDESCRIPTOR4.CpuTranslatedAddress = CPU physical
base of a CPU-visible segment (one contiguous range); MmAllocatePagesForMdlEx contiguous block,
MmWriteCombined, freed with MmFreePagesFromMdl + ExFreePool after the segment is no longer used.

AGX/ASAHI CONTRACT: UAT pages 16 KiB; any 16 KiB-aligned contiguous guest page group is publishable;
the m1n1 broker registers non-reserve pages as shared system backings (REGISTER_SHARED_BACKING, one
record per 16 KiB page, capacity 131072 = 2 GiB) and translates IPA -> PA through stage 2.

TRANSLATION: segment-2 offset -> IPA = block IPA + offset (always 16 KiB-contiguous) -> leaf
published with System backing kind (frame reference + REGISTER_SHARED_BACKING) instead of the
reserve's local grant; CPU paging/present/copy paths that read segment 2 use a kernel WC mapping of
the block instead of the reserve mapping.

WHAT IS STILL UNKNOWN: whether firmware-side consumers (context 63 residency map at 0x1500000000,
backend image arena, G4 outputs that take a CPU token/physical address) depend on segment-2 pages
being inside the m1n1 reserve; resolve by code reading before implementation (listed below).

## Code inventory (subagent report, 2026-10-13; file:line at integration 9523faf4)

Two views must replace today's single "local view":
- VidMm segment view (block): segment-2 PTE resolution, publication, CPU paging, present/copy reads.
- Reserve view (m1n1): private storage, backend arena, firmware/table objects.

Sites that read the local view or segment 2 (RA = drivers/apple-agx/render-admission):
- Reporting: RA/src/memory_windows.c QuerySegment4 (96-162) and QuerySegment5 (40-93) local
  descriptor (G3 overrides: CpuVisible, CpuTranslatedAddress = local view IPA,
  PopulatedByReservedDDRByFirmware); render_memory.c AdmissionMemoryCpuVisibleLocalBase (6-32).
- Runtime: RA/src/memory_runtime_windows.c Start (245-560: borrow local 333, zero 355,
  ResidencyMap64K 384 maps local at 0x1500000000 in context 63, partition 520-531), LocalView (841),
  ScanoutView (846-863), ExecutePaging (1049-1089 on LocalObject), PresentReadMemory (1102-1142),
  ExecutePresent (1153-1254), ResolveLocal (663-681), ReadResident (683-755).
- G3 paging (RA/src/gpuva_g3_paging_windows.c): UpdateLeaf (401-617: ResolvePageAddress with the
  local base 455; 64K check 463-467; MappingAcquire only for SegmentId 0 at 498; complete check
  517-529; kind choice 532-536; tallies 578-590; releases 593-613); RetireSystemSubtree (54-91),
  ResetTableShadow (105-119), EncodeVirtualPaging (759-919), MapPagingIpa (921-950),
  ExecuteVirtualPaging (975-1019), BuildPagingBuffer table checks (1321, 1374-1385).
- G3/G4 (RA/src/gpuva_g3_windows.c): ResolveTable (2255-2300, page tables live in segment 2!),
  CopyEscape (1337-1386), PresentTranslateSource/Read/Write/ExecutePresentVirtual (2962-3145),
  G4LogicalEnvelopeAccess (3160-3192), G4OutputInLocal/ResolveOutput (3239-3292),
  OutputMatchesLocal (2373-2401), DestroyProcess releases (2074-2081), CreateProcess
  SharedBackingGeneration = local IPA (1975-1998).
- Physical paths: paging_windows.c TRANSFER/FILL/DISCARD (82-127) via render_memory.c planners and
  shared apple_agx_physical_paging.c (local base/bytes only); gdi_windows.c (56-140),
  umd_render_windows.c (534-545), render_dynamic_windows.c (56-186), present_windows.c (61-300),
  render_present.c (33-51); scanout_windows.c (964+, fixed panel segment 2).
- Graph (drivers/apple-agx/shared/src/apple_agx_gpuva_g3_graph.c): TryLeafBacking (470-616: System
  needs a frame with Mappings > 0; Local uses SharedBackingGeneration; both send
  REGISTER_SHARED_BACKING), revoke_unused_backing keeps Local backings (235-258).
- Broker (m1n1_windows/src/hv_agx_gpuva_v5.c register_backing 403-466): an IPA inside the configured
  local reserve takes the bitmap grant path (pa == ipa required); anything else becomes a system
  record (capacity 131072).
- Tests pinning two segments / local constants: tests/fixtures/g3_segment_query_replay.inc,
  g3_memory_lifecycle_replay.inc (PageCount 36, LocalAllocationBytes 0x3b800000),
  test_g3_reserved_ddr_segment_flags.py, test_gpuva_g3_cpu_visible_segment.py,
  test_apple_agx_render_memory.py, render_memory_test.c, g3 replay scenarios (raw PTE flags 0x41 =
  segment 2), test_g4_segment_placement_replay.py.

Critical: VidMm page tables (ResolveTable, UpdateParent flags `1 | 2 << 5`) are themselves segment-2
allocations. Moving segment 2 to the block moves the page tables into Windows memory too; the
broker's table registration and UAT walk must accept table pages outside the reserve (check
hv_agx_gpuva_v5 table registration and AppleAgxGpuvaG3MirrorTable before implementing).

## Ladder (one hardware checkpoint each)

1. f2a (offline): split the memory contract into segment view (block) and reserve view; replays
   GREEN with the block simulated at a distinct IPA.
2. f2b (hardware): segment 2 backed by the block, reserve unchanged in size; full pipeline PASS,
   receipts show leaves published as shared system backings for block pages.
3. f2c (hardware): shrink the m1n1 reserve to the firmware regions; size the block per machine
   (registry override, fraction of RAM).

## Revision (same day): a third segment after all

Re-reading the consumers changed the decision. Segment 2 also holds the scanout primaries (fixed
panel segment 2, scanout_windows.c, read by the DCP through its own DART), the VidMm page tables
(PageTableSegmentId 2, read by the KMD through the CPU view) and every physical-path allocation
(GDI/present/dynamic paths require segment 2). Re-backing segment 2 would move display surfaces into
Windows memory the DCP DART does not map. The broker itself is not a constraint: `register_table`
accepts any translated IPA, and the UAT tables are KMD-owned mirrors (`AdmissionGpuvaG3MirrorTable`).

New decision: segment 3 = CPU-visible `PopulatedFromSystemMemory` memory segment backed by the KMD
block, used only by Mesa class BOs (GPU access by VA only, never scanned out, never page tables,
never allocation-list/physical). Segment 2 stays the firmware reserve for display, tables and
firmware-adjacent allocations and shrinks later. The ~40 tests that pin two segments change
mechanically; the generic plumbing that must learn segment 3 is: reporting (QuerySegment4/5,
QUERYSEGMENTCOUNT, the G3 admission contract and topology), placement, PTE resolution
(`AppleAgxGpuvaG3ResolvePageAddress`), leaf publication (System backing kind for segment 3), the CPU
paging executor (`AdmissionG3MapPagingIpa`, `AdmissionG3EncodeVirtualPaging`), physical paging 0<->3
(`apple_agx_physical_paging.c` planners) and the copy escape if class BOs use it.

Ladder (replaces the one above):
1. f2a: segment 3 reported and backed by the block, no allocation uses it (one variable: three
   segments). Checkpoint: adapter starts, every EXP1246 check passes.
2. f2b: CPU-visible class BOs prefer segment 3 with paging/publication support. Checkpoint: full
   pipeline; segment-3 groups published, no refusal.
3. f2c: shrink the m1n1 reserve; size the block per machine.

## f2b work list (EXP1247 confirmed f2a; order of implementation, each with its test)

A. Pool view: `AdmissionMemoryPoolView` (pool_windows.c) -- done in f2a.
B. Translation (shared): resolve PTE segment 3 to pool IPA + offset (bounded); segment 4+ invalid.
   Test: apple_agx_gpuva_g3_translation_test.c.
C. Leaf publication (gpuva_g3_paging_windows.c UpdateLeaf): segment-3 PTEs resolve through the pool
   view; 64 KB PTEs allowed for segment 3 (64 KiB-aligned); a group is complete when four contiguous
   16 KiB-aligned entries lie inside the pool; backing kind System; frame references
   (MappingAcquire/Release) for segment 0 and 3 alike, also in RetireSystemSubtree,
   ResetTableShadow and DestroyProcess. Pool tallies through the shapes receipt (trait 0x10).
   Test: g3_system_lifetime_cases.c (segment-3 update publishes a System backing; unmap releases).
D. Virtual paging (CPU executor): `AdmissionG3MapPagingIpa` maps segment 3 through the pool view;
   `AdmissionG3EncodeVirtualPaging` direction/coalescing rules for segment 3.
E. Physical paging: TRANSFER 0<->3, FILL 3, DISCARD 3 through the planners (render_memory.c,
   shared apple_agx_physical_paging.c) with the pool CPU base. Test: physical paging test.
F. Consumers of segment ids: `AdmissionG4LogicalEnvelopeAccess`, present virtual source
   translate/read, copy escape (predicates 58/61) accept segment 3 via the pool view.
G. Placement: CPU-visible class BOs set {aperture, local, pool}, preferred pool then local.
   Test: test_g4_segment_placement_replay.py.
Hardware checkpoint after G (EXP1248): full pipeline, pool groups published, no refusal.
