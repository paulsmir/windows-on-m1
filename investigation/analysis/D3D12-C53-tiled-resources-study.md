# D3D12 C53: tiled resources study (Tier 1, then Tier 2) for FL 12_0

Read-only design study from 2026-10-22. Nothing here is implemented yet.

Sources:
- WDK 26100: `d3d12umddi.h`, `d3dukmdt.h`, `d3dkmddi.h`.
- Mesa Asahi: Honeykrisp `hk_image.c`, `hk_image_view.c`, `hk_queue.c`, `hk_physical_device.c`; `layout/`; `compiler/`.
- This repository: the UMD, the winsys and the KMD.

[K] marks Microsoft-documentation knowledge that has not been checked against a header.

## Headline

- **Tier 1 is reachable.** It needs:
  - remapping a reserved VA through MapGpuVirtualAddressCb with an allocation offset (OffsetInPages) or through UpdateGpuVirtualAddressCb. The KMD already accepts any `AllocationOffsetInBytes` (`AdmissionG3UpdateLeaf`).
  - the AGX hardware sparse page table for textures (texture/PBE descriptor in `Image Mode=Sparse`, 16 KiB entries; Honeykrisp's `ail_initialize_sparse_table`).
  - per-device zero (read-only) and scratch (read-write) 64 KiB backing for NULL tiles, as Asahi does.
- **Tier 2 is blocked by min/max reduction filtering.** Asahi `00-hk-defaults.conf` documents that Apple hardware lacks it and fakes it for vkd3d FL12_0. Shader emulation is exact only for point and bilinear filtering.
- **FL 12_0 may also need 40 VA bits per resource [K].** The UAT is 39-bit and the driver reports 32. Check the requirement and whether the runtime enforces it before investing.

## WINDOWS CONTRACT

Reserved resources:
- They go through `pfnCreateHeapAndResource`. Hypothesis: the heap is NULL and `ReuseBufferGPUVA` is NULL. Measure this with the existing receipt.
- Textures use layout `64KB_UNDEFINED_SWIZZLE`. Only that layout can be claimed, because AGX GPU tiling is not D3D standard swizzle.

Mapping DDIs:
- `pfnGetMipPacking`: number of packed mips and their tiles, per slice.
- `pfnUpdateTileMappings` and `pfnCopyTileMappings` (queue table): regions (UseBox, subresource spill), ranges with NULL/SKIP/REUSE_SINGLE_TILE flags, `NO_HAZARD`. They are ordered on the queue.
- `pfnCopyTiles` (list): tile region <-> linear buffer, 64 KiB per tile, row-major.

Formats and MSAA:
- `D3D12DDI_FORMAT_SUPPORT_TILED` is set per format.
- Tiled MSAA quality levels are 0.

Tier requirements [K]:

| Tier | Requires |
|---|---|
| 1 | Buffer and Texture2D; NULL access undefined but must not remove the device |
| 2 | NULL reads 0 and NULL writes dropped; mips of at least one tile are not packed; Sample with clamp and status; `CheckAccessFullyMapped`; MIN/MAX filters |
| 3 | Texture3D (G13 cannot provide this) |

## AGX/ASAHI CONTRACT

Tiles and layout:
- The hardware page is 16 KiB. A D3D 64 KiB tile is 2x2 AGX tiles, and `ail_get_max_tile_size` doubles to exactly the D3D shapes.
- Images are GPU-tiled and uncompressed. The mip tail starts below one 16 KiB tile, so the Tier 1 and Tier 2 "not packed" rules hold.
- Honeykrisp uses a single mip tail. D3D needs a tail per slice on a 64 KiB-aligned layer stride, which requires an ail layout change.

The sparse table:
- It is made of folios of 256 4-byte `{Address>>14, Enabled}` entries.
- A disabled page reads 0 and reports non-resident to the texture instruction (the `sparse` bit and residency code are lowered in `agx_nir_lower_texture.c`).

Writes and buffers:
- Writes go through the VA. An unbound RW page maps a scratch page, so writes are dropped.
- Buffers keep a read-only shadow VA where unbound pages map the zero BO (`hk_buffer_address`).
- Buffer UAV reads of NULL tiles return scratch contents, not 0.

Other limits:
- No sparse depth/stencil, no 3D, no MSAA sparse.
- Min LOD needs `txb`/`txd` (plain `tex` with min_lod is unimplemented).
- Gallium asahi has no sparse support. The owner path needs a port: a virtual BO, the sparse table, and the descriptor sparse mode.

## Current repository

- **Heaps.** Each heap is one owner BO, bound whole at offset 0: `AgxWin32GpuvaBind` reserves and maps it entirely, the `AGX_WIN32_GPUVA_OPS.Map` signature has no offset parameter, and `umd_gpuva_windows.c` sets `OffsetInPages = 0`.
- **Placed textures.** Placed 2D textures live in their own twiddled BO (C36), not in heap memory.
- **Buffer VA lookup.** Buffer VA resolution goes through `HeapAt`, which a reserved VA would not satisfy.
- **ExecuteCommandLists.** It replays into one owner synchronously under a lock, which permits a CPU-synchronous first UpdateTileMappings.
- **KMD (GpuMmu, VidMm tables).**
  - `UpdateLeaf` handles 4 KiB and 64 KiB PTEs and any allocation offset. It publishes a 16 KiB AGX page only from a complete, contiguous 4x4 KiB group.
  - It rejects Zero PTEs.
  - Unknown paging operations return NOT_SUPPORTED, which leads to bugcheck 0x10E.
  - Hardening is needed before tiled work: COPY_PAGE_TABLE_ENTRIES, and never letting Zero PTEs arise (keep `ZeroInPteSupported=0`; use zero/scratch BOs and no Zero reservations).

## TRANSLATION (proposed)

- **Reserved buffer.** Two VA ranges: RW (the resource VA) and an RO alias. Both start mapped to scratch or zero. A reserved-resource registry next to `HeapAt` makes CBV/SRV use RO and UAV use RW. The owner needs a virtual BO whose residency is the set of heaps mapped into it.
- **Reserved Texture2D.** An owner texture that is GPU-tiled and uncompressed, with a sparse table and sparse-mode descriptors. Writes go through an RW VA that starts as scratch.
- **UpdateTileMappings version 1** (CPU-synchronous under the owner lock):
  - Walk regions and ranges per D3D tile.
  - Buffers: MAP 16x4 KiB pages at `heap offset * 64 KiB` on RW and RO.
  - Textures: write 4 sparse-table entries per tile pointing at the heap VA.
  - Wait for the paging fence.
  - Keep a shadow map so CopyTileMappings can replay MAP operations (never use the COPY operation).
- **UpdateTileMappings version 2.** UpdateGpuVirtualAddressCb on the queue context with its fence, plus GPU writes to the sparse table.
- **GetMipPacking.** From the ail mip tail per 64 KiB-aligned slice.
- **CopyTiles.** Byte copy for buffers; `resource_copy_region` per tile for textures.
- **Tier 2 additions.**
  - Shaders: the SPIR-V SparseResidency capability, and status operands from dxil-spirv.
  - Buffer `CheckAccessFullyMapped`: needs a software residency bitmap.
  - Min/max filtering: emulate, or do not claim Tier 2.

## Phases (hardware checkpoints; about 18-25 experiments)

| Phase | Experiments | Content |
|---|---|---|
| P0 | 2 | Receipts for the reserved-resource call shapes (behind a gate) |
| P1 | 2-3 | Remap primitive on a test heap (offset maps, remap to scratch, no 0x10E) |
| P2 | 3 | Tier 1 buffers (NULL, SKIP, REUSE, CopyTileMappings) |
| P3 | 4-6 | Tier 1 2D textures: sparse table, tile shapes per bpp and BC, packed mips, RTV/UAV writes |
| P4 | 3-4 | Tier 2 residency feedback and LOD clamp |
| P5 | 2-3 | Min/max emulation against WARP |
| P6 | 2 | Conformance subset |

## WHAT IS STILL UNKNOWN

1. Min/max filtering: whether an approximate emulation is acceptable.
2. The FL 12_0 VA-bit requirement and whether the runtime enforces it.
3. Whether VidMm ever emits COPY_PAGE_TABLE_ENTRIES or Zero PTEs. Both paths risk bugcheck 0x10E today.
4. Ordering of MapGpuVirtualAddressCb over live mappings, and of UpdateGpuVirtualAddressCb against the owner context.
5. Whether mixed 16 KiB/64 KiB PTE choices can leave an incomplete 16 KiB group, which would fault.
6. Packed mips per slice and the Tier 1 rule for arrays.
7. Buffer UAV reads of NULL tiles (scratch, not 0).
8. The size of the Gallium sparse port.
9. D3D12 has no buffer-only tier, so a buffer-only stage is development only.
