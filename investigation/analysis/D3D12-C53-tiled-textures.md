# D3D12 C53: reserved Texture2D and VA mapping scale

Status: design, 2026-10-22 (offline). Follows D3D12-C53-tiled-buffers-plan.md
(B0-B3, validated by EXP1886 HW1). Two steps:
- B4: reserved ranges scale to real resource sizes;
- T2D: reserved Texture2D over the AGX sparse page table.

Tier 1 stays unreported until both work: Tier 1 includes Texture2D.

## B4: the reserved-range scale limit (a real defect)

Observed in source, `umd_internal.h` and `agx_win32_asahi_bo.c`:
- The UMD alias table is a fixed `VaAliases[1024]`.
- `AgxWin32AsahiCreateVirtualBo` maps every 64 KiB tile separately onto
  offset 0 of the device scratch BO. Each such map is one alias entry, and
  scratch entries never coalesce because all of them map the same scratch
  offset.

So a reserved resource of more than about 1000 tiles (64 MiB) fails at
creation, and every live reserved range shares the same 1024 entries. A
virtual-texturing title reserves GiBs. Creation also costs one
`pfnMapGpuVirtualAddressCb` per tile.

WINDOWS CONTRACT:
- `pfnMapGpuVirtualAddressCb` maps a contiguous run of allocation pages at a
  VA inside a range reserved by `pfnReserveGpuVirtualAddressCb`. This is what
  B0 uses.
- `pfnUpdateGpuVirtualAddressCb` (Microsoft Learn, "PFND3DDDI_UPDATEGPUVIRTUALADDRESSCB"
  and "_D3DDDI_UPDATEGPUVIRTUALADDRESS_OPERATION") is the tiled-resource
  callback. In one batch it accepts:
  - MAP with `AllocationSizeInBytes` < `SizeInBytes`: the same allocation
    range is repeated over the VA range (REUSE_SINGLE_TILE, scratch fill);
  - UNMAP to the zero or no-access state;
  - COPY of mappings (CopyTileMappings).

  The page-table updates run on a paging context, after the rendering
  context signals `FenceValue` on `hFenceObject`. The paging context then
  signals `FenceValue + 1`.

AGX/KMD CONTRACT (source):
- VidMm turns either callback into DxgkDdiUpdatePageTable. The KMD leaf path
  (`gpuva_g3_paging_windows.c` AdmissionG3UpdateLeaf) handles `Flags.Repeat`.
- It refuses `pte->Zero`, and an invalid PTE leaves the page unmapped, so an
  access to it faults. Unmapped tiles therefore keep mapping a scratch BO, as
  in B1.

TRANSLATION (B4, no new WDDM callback, deterministic and host-testable):
1. **Alias table.**
   - The table grows: a heap array, doubled under ScreenBufferLock before
     each map, with Room computed as now. There is no fixed limit.
   - `Add` merges the new alias with a neighbour that has the same token and
     protection and is contiguous in both VA and allocation offset. A buffer
     mapped tile k -> heap tile k is then one entry.
2. **Tile scratch.** The winsys owns one 1 MiB scratch BO (16 tiles),
   created on first use.
   - A run of unmapped tiles maps in chunks of up to 16 tiles, one map call
     and one alias entry per chunk. A 1 GiB reserved range then needs 1024
     calls, down from 16384.
   - Reads of unmapped tiles return scratch contents, which Tier 1 allows
     (undefined).
   - The submission expansion (`add_bo`) lists the tile scratch BO for a
     virtual BO with any unmapped tile.

Deferred, as a performance step with its own hardware checkpoint: queue-ordered
`pfnUpdateGpuVirtualAddressCb`.
- It needs a dedicated monitored fence, signalled from the render context
  before each batch. The render fence cannot be used: the paging context's
  `FenceValue + 1` signal would collide with our submission sequence.
- It would replace the CPU retire in `MapVirtualTiles`, batch maps, repeat
  scratch with a single tile, and implement CopyTileMappings as COPY.
- WHAT IS STILL UNKNOWN for it: VidMm's behaviour on this KMD (paging-context
  ordering, Repeat leaf updates from a MAP with `AllocationSizeInBytes`).

## T2D: reserved Texture2D

WINDOWS CONTRACT (D3D12 tiled resources, Microsoft Learn "Volume tiled
resources" / "Tiled resources texture sampling features",
D3D12DDI_DEVICE_FUNCS_CORE pfnGetMipPacking):
- A reserved Texture2D uses `D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE`:
  - every 64 KiB tile has the standard tile shape for the format
    (32 bpp: 128x128; 64 bpp: 128x64; 128 bpp: 64x64; 16 bpp: 256x128;
    8 bpp: 256x256);
  - the texel order inside a tile is the driver's.
- The runtime derives subresource tiling from the standard shapes. The driver
  answers only `pfnGetMipPacking`: NumPackedMips and NumTilesForPackedMips.
  - Packed mips are mapped like any tiles, by the D3D12_PACKED_TILE
    subresource coordinate (X = tile index in the pack).
  - A mip whose size is below the standard tile shape in either dimension is
    packed. The driver may pack more.
- UpdateTileMappings regions:
  - with UseBox, a box of tiles in one subresource;
  - otherwise NumTiles tiles in resource tile order from the start
    coordinate: subresource by subresource, each in X, then Y, then Z order,
    and the packed tiles of each array slice after its unpacked mips.
- Tiers (Microsoft Learn "D3D12_TILED_RESOURCES_TIER", "D3D12_TEXTURE_LAYOUT"):
  - Tier 1: GPU reads or writes of NULL mappings are undefined; a level
    whose size is a multiple of the standard tile shape is never packed.
  - Tier 2: a level at least one standard tile shape in size is never
    packed; NULL tiles read 0 and writes to them are discarded; LOD clamp
    and status (CheckAccessFullyMapped) shader operations.
  - Texture3D only from Tier 3; no Texture1D. The layout page names no array
    restriction, so arrays are part of the contract.

AGX/ASAHI CONTRACT (Honeykrisp `hk_image.c`, `hk_image_view.c`,
`hk_queue.c`; ail `layout.c`, `layout.h`; genxml):
- A hardware tile is exactly one 16 KiB page for twiddled layouts, for every
  level below `mip_tail_first_lod` (the hk_queue assert "fundamental to AGX").
  A standard tile is 2x2 hardware tiles, so the D3D12 standard shapes are
  exactly 2x2 hardware tiles for 8-128 bpp.
- Binding one standard tile at memory offset M maps four pages, from
  `ail_get_twiddled_block_B(layout, level, x, y, z)` for
  (2tx,2ty) -> M, (2tx+1,2ty) -> M+16K, (2tx,2ty+1) -> M+32K, (2tx+1,2ty+1) -> M+48K
  (`bind_hw_tile`). A hardware tile outside the level is not bound.
- The mip tail starts at `mip_tail_first_lod`, the first level smaller than
  one hardware tile. It is bound as one contiguous range per layer of
  `mip_tail_stride` bytes from `level_offsets_B[mip_tail_first_lod]`.
- Sparse page table:
  - A sparse image has a zeroed BO of `layout.sparse_table_size_B`.
  - The texture descriptor of a level below the mip tail uses
    `AGX_IMAGE_MODE_SPARSE`, with
    address = table VA + `ail_page_to_sparse_index_el(layout, layer, page)` x 4.
  - Each page's 4-byte Sparse Block is {Address = page VA >> 14,
    Enabled bit 31, bit 30 = Enabled (Honeykrisp)}. A disabled entry reads 0
    and reports non-resident.
  - Every bind updates both the VA mapping and the table entries for the
    pages it covers (hk_flush_bind).
- The image VA itself stays bound to scratch where unmapped, so PBE writes,
  image stores and lowered atomics to unmapped tiles land in scratch.
- Compression is off for sparse images (`hk_can_compress`).
- Gallium `agx_pack_texture` (agx_state.c) has no sparse mode. It always
  uses `cfg.first_level` with the level-0 address, so there is no per-level
  address offset: the descriptor can point at the layer's table base for
  every view.

TRANSLATION:
1. **Layout.** A reserved Texture2D is an Asahi pipe_resource created with
   the plain twiddled modifier (no compression). It is restricted to formats
   of 8, 16, 32, 64 or 128 bpp, with no MSAA and no depth. Its BO is swapped
   for a virtual BO of `layout.size_B` rounded up to 64 KiB, as reserved
   buffers do. A sparse-table BO of `sparse_table_size_B`, zeroed and CPU
   mapped, hangs off the resource.
2. **Mip packing.**
   - first_packed = the first level whose size is below the standard tile
     shape in X or Y.
   - The packed region is
     [level_offsets_B[first_packed], level_offsets_B[levels]) of the layer.
     Levels between first_packed and the AGX mip tail consist of whole
     16 KiB hardware tiles, so the region starts page-aligned.
   - NumTilesForPackedMips = ceil(region / 64 KiB). Packed tile k maps 4
     consecutive VA pages of the region (the last one clipped).
   - Arrays: one pack per slice, ordered after that slice's mips (the AGX
     tail of each layer is `mip_tail_stride` bytes at the layer's tail
     offset; Honeykrisp fans one tail bind out per layer the same way).
     The first implementation refuses ArraySize > 1 (fail-closed receipt);
     arrays must work before the Tier 1 flip.
   - This packing rule meets the Tier 2 guarantee as well (levels of at
     least one standard tile are never packed).
3. **Tile walk.** AgxTileMappingWalk is generalised from buffer X runs to
   resource tile indices, via a pure helper with host tests:
   - (subresource, x, y, z) -> linear index, and a box -> rows of runs;
   - linear index -> {subresource, tx, ty} or {packed, k}.
4. **Bind.** A run of resource tiles bound to heap tile H, or scratch for
   NULL:
   - unpacked tile: 4 page maps (or fewer at a level edge) at the
     twiddled-block VAs onto heap pages 4H..4H+3;
   - packed tile k: 4 consecutive page maps.

   In both cases the Sparse Block entries of the same pages are written
   (Enabled with the new page VA, or disabled for NULL). Pages are mapped by
   `MapAt` at 16 KiB granularity; the virtual BO's per-tile backing array
   becomes per page.
5. **Ordering.** As B1: retire the submissions naming the resource, then
   update VA and table on the CPU. The table is written through the CPU
   mapping of its BO; it is a write-combined Asahi BO, and the next
   submission reads it.
6. **Descriptor.** Overlay in `build-native-asahi-state.py` on
   `agx_pack_texture`: when the resource carries a sparse table,
   `cfg.mode = AGX_IMAGE_MODE_SPARSE` and
   `cfg.address = table VA + ail_page_to_sparse_index_el(layout, first_layer, 0) * 4`.
   The resource also stays in `add_bo` residency through its table BO.
7. **Tier 1 flip** (separate commit and experiment): TiledResourcesTier 1,
   FORMAT_SUPPORT tiled bits for the supported formats, and GetMipPacking.

WHAT IS STILL UNKNOWN:
- Whether the texture unit tolerates a sparse descriptor with levels in the
  mip tail when the table entries of those pages are enabled. Honeykrisp
  never builds that view; it switches to a plain descriptor for a base level
  in the tail. Hardware checkpoint HW2: sample every level of a reserved
  mipped texture.
- Coherence of CPU table writes for the next submission (write-combined BO).
  HW2 also covers it: remap, then sample, in two submissions.

Checkpoints:
- offline: tests for the walk, the page bind plan and the alias table;
- builder: syncheck;
- HW2: self-test of a reserved 512x512 RGBA8 texture with 3 mips (tiles
  mapped out of order, one NULL, packed tail), sampled to a readback buffer;
  expect heap patterns and 0 from the NULL tile.
