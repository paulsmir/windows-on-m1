# D3D12 C53: tiled resources Tier 1, buffers first -- implementation plan

This plan was written offline on 2026-10-22 with a planning agent, from the
WDK 26100 headers and the repository. It extends
D3D12-C53-tiled-resources-study.md. Markers: [K] is Microsoft-documentation
knowledge, [H] is a hypothesis.

## Truthful capability

- Keep TiledResourcesTier NOT_SUPPORTED until both reserved buffers and
  reserved Texture2D work.
- Then flip Tier 1, FORMAT_SUPPORT_TILED and the wired DDIs in one commit,
  validated by an experiment of its own.
- Reporting Tier 1 with every TILED format bit clear is rejected: Tier 1
  includes Texture2D, and test_d3d12_abi_gate guards the claim.
- While the tier is 0, the runtime refuses CreateReservedResource [K], so no
  reserved resource can reach the driver before the flip. Earlier hardware
  checkpoints therefore use an off-by-default self-test gate,
  APPLE_AGX_D3D12_RESERVED_SELFTEST=1, read at CreateDevice.
  - It drives the internal machinery and the real UpdateTileMappings,
    CopyTileMappings and CopyTiles, with heaps created inside the driver.
  - It reports through receipts only.
  - It never touches GetCaps: an offline test checks that the caps are
    byte-identical with the gate set.

## UMD (agx_d3d12_ddi.cpp)

- A reserved buffer is an AgxD3d12Heap with `reserved` set and
  `tiles` -> AgxD3d12Reserved, which records per 64 KiB tile `{heap, heap_tile}`
  (NULL means a NULL tile). The heap's `native` is a virtual owner buffer, and
  the heap goes through RegisterHeap, so the HeapAt and MirroredHeapAt call
  sites work unchanged (the native side gives agx_map_gpu()+offset, which is
  the reserved VA).
- CreateHeapAndResource gets a reserved branch before the placed-buffer one,
  taken when the heap description is absent and the ReuseBufferGPUVA resource
  is NULL [H; receipt measure-d3d12-reserved].
  - It uses own_heap with no CPU mapping, so MapHeap refuses it.
  - It refuses widths above 32 bits (MaxGPUVirtualAddressBitsPerResource 32).
- DestroyHeapAndResource and ReleaseHeap release the reserved heap: unregister
  it, then destroy the owner buffer.
- CheckExisting reports allocation 0 [H].

## GPU VA path

- AGX_WIN32_GPUVA_OPS gains an optional MapAt with an allocation offset in
  pages. The UMD implements it as map_va_at: OffsetInPages, no change to
  CanonicalGpuVa, and an entry in the alias table.
- free_va drops aliases. replace_canonical and map_canonical_as replay a
  token's aliases with the fresh allocation before freeing the old one; this
  covers the EXP1006/EXP1011 remaps.
- Winsys: AgxWin32GpuvaReserveRange (NO_ACCESS only; never RESERVE_ZERO),
  MapRange (64 KiB aligned, bounds-checked, result 3 is terminal, then
  WaitPaging), and FreeRange.
- Residency: add_bo expands a virtual BO into its distinct backing BOs, and
  the virtual token never reaches MakeResident. NULL tiles map the device
  scratch BO, which is already in every submission and is 64 KiB on GPUVA.
  The batch limits include the expansion, and a written virtual BO marks its
  backing BOs written.

## Owner (agx_d3d12_windows.cpp, agx_win32_asahi_bo.c)

- windows_bo gains a Virtual mode: the address is the reserved VA, the handle
  is a small token, and it holds a reference to each backing BO.
- dispose frees the range and drops the references.
- native_map gathers into a shadow and scatters back on publish (never
  AGX_WIN32_ASAHI_FAIL).
- New API:
  - AgxD3d12OwnerCreateReservedBuffer: every tile starts on scratch.
  - AgxD3d12OwnerMapReservedTiles: runs of `{first, tiles, backing|NULL,
    backing_tile}`, under OwnerLock, refused while a submission holds the BO.

## KMD

- AdmissionG3UpdateLeaf already accepts any AllocationOffsetInBytes, 4 KiB
  and 64 KiB PTEs, and remaps safely (reference-counted).
- It rejects Zero PTEs, and ZeroInPteSupported stays 0.
- COPY_PAGE_TABLE_ENTRIES and NOTIFY_RESIDENCY return NOT_SUPPORTED, which
  means bugcheck 0x10E. So v1 never uses UPDATEGPUVIRTUALADDRESS_COPY:
  CopyTileMappings replays MAP operations from the shadow.

## DDIs

- GetMipPacking (CORE_0010 slot, inherited by 0075): buffers report 0/0.
- UpdateTileMappings and CopyTileMappings (queue CORE_0001) run
  CPU-synchronously under the owner lock, as ExecuteCommandLists does.
- CopyTiles (list 3D_0003, inherited).

## Phases

| Phase | Work | Offline tests |
|---|---|---|
| B0 | GPUVA MapAt/MapRange/ReserveRange and alias replay | agx_win32_gpuva_test, plus fixtures for map_va_at and replace_canonical |
| B1 | Virtual BO, add_bo expansion, owner API | Batch fixture |
| B2 | Pure walker AgxTileMappingPlan (NULL, SKIP and REUSE flags, defaults, coalescing, bounds) and CopyTileMappings snapshot semantics | test_d3d12_tile_mapping_walk; classification fixture |
| B3 | DDIs wired but unreachable, plus the self-test gate | Caps byte-identical |
| T2D | Texture2D (separate plan) | -- |
| FLIP | Report Tier 1 and the TILED format bits | -- |

Hardware checkpoints:

- **HW1, after B1, through the self-test.**
  - Map a 4-tile virtual buffer R onto heap A: tile 0 to A2, tile 1 NULL, tile 2 to A0.
  - Copy R to readback. Remap tile 1 to A1 and copy again.
  - Pass: the patterns match, no fault, no 0x10E.
- **HW2, after B3.** The real DDIs with SKIP, REUSE, overlapping copies and a UAV write.
- **HW3 = FLIP.** Also captures the reserved-resource call shape passively.

## Risks

- 0x10E from paging operations that are not handled, or from an UpdatePageTable wait that times out.
- Orphaned aliases after replace_canonical.
- A CPU map of a virtual BO.
- Ordering against queue Waits: WaitForFence is rejected and the update is synchronous.
- Deferred PTE population.
- Mixed 4 KiB/64 KiB PTE groups.
- Per-tile NULL map cost.
- Staging for CPU-visible heaps.
- The reserved call-shape encoding.
