/* C53 T2D (investigation/analysis/D3D12-C53-tiled-textures.md): the AGX side
 * of a reserved Texture2D. A D3D12 standard tile (64 KiB) of a GPU-tiled ail
 * layout is 2x2 hardware tiles of one 16 KiB page each (ail_get_max_tile_size
 * times 2 is the D3D12 standard shape for 1-16 byte elements); the mips from
 * the first one smaller than a standard tile on either axis are packed, per
 * array layer, into whole 64 KiB tiles of the layer's tail.
 * Pure over an ail_layout: host-tested by tests/test_c53_sparse_pages.py. */
#ifndef AGX_WIN32_SPARSE_H
#define AGX_WIN32_SPARSE_H

#include <stdint.h>
#include "asahi/layout/layout.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AGX_WIN32_SPARSE_TILE_B 0x10000u

/* The pages one standard or packed tile covers. Offset is the byte offset of
 * the page in the image (layer included); HeapPage is the 16 KiB page of the
 * 64 KiB heap tile it maps (Honeykrisp bind_hw_tile: +1 for the odd column,
 * +2 for the odd row; packed tiles map their pages in order); TableIndex is
 * the page's Sparse Block index (ail_page_to_sparse_index_el). */
typedef struct {
  unsigned Count;
  uint64_t Offset[4];
  unsigned HeapPage[4];
  unsigned TableIndex[4];
} AGX_WIN32_SPARSE_PAGES;

/* The D3D12 tiling of a layout: StandardMips, standard tiles per mip, and
 * packed tiles per layer (0 without packed mips). 0 if the layout cannot be
 * reserved: not GPU-tiled, compressed (metadata), multisampled, 3D, or an
 * element size other than 1/2/4/8/16 bytes. */
int AgxWin32SparseTiling(const struct ail_layout *Layout, unsigned *StandardMips,
                         unsigned *TilesX, unsigned *TilesY, unsigned *PackedTiles);
/* Standard tile (TileX, TileY) of Mip in Layer; 0 if outside. */
int AgxWin32SparseStandardTile(const struct ail_layout *Layout, unsigned Layer, unsigned Mip,
                               unsigned TileX, unsigned TileY, AGX_WIN32_SPARSE_PAGES *Pages);
/* Packed tile Tile of Layer; pages past the layer's last mip are left out.
 * 0 if outside or the layout has no packed mips. */
int AgxWin32SparsePackedTile(const struct ail_layout *Layout, unsigned Layer, unsigned Tile,
                             AGX_WIN32_SPARSE_PAGES *Pages);
/* A Sparse Block (genxml cmdbuf.xml): Address >> 14 in bits 0-25, bit 30 and
 * Enabled bit 31 for a mapped page (as Honeykrisp hk_flush_bind), 0 for an
 * unmapped page, which reads 0 and reports non-resident. */
uint32_t AgxWin32SparseBlock(uint64_t PageVa, int Enabled);

#ifdef __cplusplus
}
#endif

#endif
