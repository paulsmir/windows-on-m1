# D3D12 C21G: general Texture3D layouts (formats, sizes, mip chains)

Requested by the pipeline session (2026-10-23, after EXP1902). EXP1902
refused an RGBA32F 4x4x4/2-mip volume: `VolumeLayoutInfo` reason 1, the
allocation size became ~0, and the runtime threw E_OUTOFMEMORY. C21
(`D3D12-C21-resolved-layout-contract.md`) admits only 4-byte formats
(R32_UINT, RGBA8), square power-of-two sides up to 32, and at most two
levels. Real applications need RGBA16F/RGBA32F/R11G11B10/R8 volumes, sides
up to 2048 and full mip chains (LUTs, fog/froxel grids, noise volumes).

## Sources inspected

- WDK 26100 `d3d12umddi.h`: D3D12DDI_SWIZZLE_BIT_ENTRY (Valid, ChannelIndex
  X/Y/Z/SS, SourceBitIndex), SWIZZLE_PATTERN_DESC_0003/0022,
  RESOURCE_ALLOCATION_INFO (one MipLevelSwizzleTransition) and
  ALLOCATION_INFO_0022 (MipLevelSwizzleTransition[5]), TEXTURE_LAYOUT caps.
- Current DDI: VolumeLayoutCaps (0003 and 0022 forms), VolumeLayoutInfo,
  CheckResourceAllocationInfo(0022), ProjectR3AllocationInfo,
  CreateVolumeResource, subresource info.
- Owner: texture3d_template / texture3d_layout / AgxD3d12QueryTexture3DLayout;
  native query `agx_resource_layout_query` (build-native-asahi-state.py).
- Asahi AIL (`src/asahi/layout/layout.c`, `layout.h`, `tiling.cc`):
  `ail_get_max_tile_size`, `ail_initialize_gpu_tiled`, `ail_space_bits`,
  `ail_get_twiddled_block_B`, `memcpy_small`, and `mipmapped_z` (which only
  sets the number of allocated levels).
- Live receipts (EXP1902): an R2 runtime asks only for the 0022_TEXTURE_LAYOUT
  counts. It never asks for an individual layout or pattern descriptor, even
  when creating a volume. The 0022 path already writes one transition into
  all five slots, and those layouts are created on hardware (C21), so the
  runtime accepts equal transitions (empty slots).

## AGX/ASAHI CONTRACT (GPU-tiled, uncompressed)

- Per level l there is a tile of `tilesize_el[l]` (w x h elements).
  - "Large" levels, where both sides are at least the max tile, use the max
    tile of 16 KiB: 1 B 128x128, 2 B 128x64, 4 B 64x64, 8 B 64x32, 16 B 32x32.
  - Smaller levels (the POT miptree) use a square tile of
    POT(min(w_l, h_l)), stored over a POT-rounded region.
- Tile index = (y / th) * ceil(stride_el[l] / tw) + x / tw. Tiles are
  tile_bytes = tw * th * blocksize apart.
- Inside a tile: element offset = space_bits(x mod tw) | space_bits(y mod th)
  << 1. That is X0 Y0 X1 Y1 ..., with the extra bits of the longer side on
  top (128x64: ... X5 Y5 X6).
- Slice z of level l starts at z * layer_stride_B + level_offsets_B[l]. The
  layer stride is constant across levels.

## WINDOWS CONTRACT

- A Texture3D allocation query returns a resolved device-dependent layout
  (0x100 + i < 0x100 + count), the size, and the mip transitions: 0003 has
  one, so two pattern slots; 0022 has five, so six slots.
- Existing allocation info and subresource info must agree, with per mip
  Offset, RowStride (bytes per row of blocks) and DepthStride.
- A swizzle pattern maps the low address bits of a block to coordinate bits.
  X is in bytes: its low log2(blocksize) bits select the byte inside the
  element.
- CPU Map of these resources stays refused, so no runtime swizzle copy runs on
  them today.

## TRANSLATION

1. Registry per element size e in {1, 2, 4, 8, 16} bytes. It is an ordered
   shape list: the max tile first, then the square POT tiles that the POT
   miptree can produce, from largest to 1 (2 B and 8 B also get a square
   tile of the long side, because POT(min) can exceed the max tile height).
   Pattern i describes shape i exactly: e byte bits, then interleaved
   X/Y texel bits, then the leftover bits.
   - Layout (e, i) has slots [shape_i, shape_i+1, ...], clamped at the last
     shape: two slots in 0003, six in 0022. Layout and pattern IDs are dense
     from 0x100. Counts are identical in both forms.
2. Per resource, from the real AIL layout (the native query, extended with
   per-level tile sizes and strides). Exact shapes are tried first: every
   C21 resource keeps its exact two-pattern layout and transition, under the
   new IDs. Only when exact shapes do not fit the slots does a second pass
   apply the square-prefix merge:
   - a level that is one single square tile whose side fits the current
     slot's shape keeps the current slot. Its Morton order is the low bits of
     the larger interleave, so every texel address is exact (the square-prefix
     rule). A square texture therefore needs one slot for all its mips;
   - otherwise the level's exact shape must be the current one or a later
     one in the list. Skipped shapes become empty slots (equal transitions);
   - more slots than the interface has: refuse, as today.
3. Subresource info: Offset = level_offsets_B[l]; RowStride =
   ceil(stride_el / tw) * tile_bytes of the level's slot shape; DepthStride =
   layer_stride_B.
4. Owner: texture3d_template admits every uncompressed, non-ZS format of 1 to
   16 bytes that the native query accepts, sides up to 2048, and up to the
   full chain. RTV/UAV/SRV stay as implemented.
5. Allocation info: 0003 gets the single transition when the resource needs at
   most two slots (otherwise refuse); 0022 gets all five.

Invariant test (deterministic, RED -> GREEN): for every element size and a
grid of widths, heights, depths and mip counts, every texel of every
requested level is addressed through registry pattern + slot + subresource
info and compared with AIL's `ail_get_twiddled_block_B` + within-tile
`space_bits`. Plus a size and bounds check against the AIL extent.

## WHAT IS STILL UNKNOWN

- Whether the runtime ever reads the pattern descriptors (no live query seen).
  If it does, they are exact for every texel; the square-prefix rule declares
  a block larger than a small level's storage, but no texel of that level
  addresses outside the storage.
- GPU behaviour of the newly admitted formats and sizes for 3D SRV/UAV/RTV.
  Checkpoint: a probe step that uploads RGBA16F/RGBA32F/R8 volumes (64^3 with
  full mips, and a non-square single-mip 160x90x16), reads each mip back
  through a shader, and compares with WARP.

## Implementation (2026-10-23)

- Registry and oracle: 6305b84d.
- Wiring:
  - DDI: VolumeLayoutCaps, VolumeLayoutInfo, VolumeAllocationQuery (2 slots
    for 0003, 6 for 0022), CheckExistingResourceAllocationInfo0022 (the
    stored five transitions), CheckSubresourceInfo (the stored row stride);
    1D moves to the registry's 4-byte 1x1 pattern (0x117).
  - Owner: texture3d_template admits formats of 1 to 16 bytes, sides up to
    2048 and 12 levels; the query carries the per-level tiles.
  - Native query: TileWidthEl, TileHeightEl, StrideEl.
- The parent's C21/C23/C24 fixtures follow the new IDs and domain; their
  fake native queries now use the linked AIL.
- Gate: BASELINE_OK, builder syncheck OK.
- Checkpoint pending: the volume probe step (WARP reference) and a J313 run.
