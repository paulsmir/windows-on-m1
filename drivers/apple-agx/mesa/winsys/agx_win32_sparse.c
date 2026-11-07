#include "agx_win32_sparse.h"
#include "util/format/u_format.h"
#include "util/u_math.h"

/* the first mip smaller than a standard tile (2x2 hardware tiles) on an axis */
static unsigned first_packed(const struct ail_layout *layout, struct ail_tile hw) {
  unsigned l;
  for (l = 0; l < layout->levels; ++l) {
    const unsigned w = util_format_get_nblocksx(layout->format, u_minify(layout->width_px, l));
    const unsigned h = util_format_get_nblocksy(layout->format, u_minify(layout->height_px, l));
    if (w < 2u * hw.width_el || h < 2u * hw.height_el) break;
  }
  return l;
}

static int reservable(const struct ail_layout *layout, struct ail_tile *hw) {
  const unsigned block = util_format_get_blocksize(layout->format);
  if (!layout || layout->tiling != AIL_TILING_GPU || layout->compressed ||
      layout->sample_count_sa != 1 || layout->mipmapped_z || !layout->levels ||
      layout->levels > AIL_MAX_MIP_LEVELS ||
      /* an array layer's tail must start on its own page */
      (layout->depth_px > 1 && (layout->layer_stride_B % AIL_PAGESIZE)) ||
      (block != 1 && block != 2 && block != 4 && block != 8 && block != 16)) return 0;
  *hw = ail_get_max_tile_size(block);
  return 1;
}

/* bytes of one layer's mips from Mip on, rounded to whole pages */
static uint64_t tail_bytes(const struct ail_layout *layout, unsigned mip) {
  return align64(layout->level_offsets_B[layout->levels] - layout->level_offsets_B[mip], AIL_PAGESIZE);
}

int AgxWin32SparseTiling(const struct ail_layout *layout, unsigned *standard_mips,
                         unsigned *tiles_x, unsigned *tiles_y, unsigned *packed_tiles) {
  struct ail_tile hw;
  unsigned l, packed;
  if (!layout || !standard_mips || !tiles_x || !tiles_y || !packed_tiles ||
      !reservable(layout, &hw)) return 0;
  packed = first_packed(layout, hw);
  for (l = 0; l < packed; ++l) {
    /* a standard mip is in the large miptree: one-page hardware tiles at a
     * page-aligned level offset */
    if (layout->tilesize_el[l].width_el != hw.width_el ||
        layout->tilesize_el[l].height_el != hw.height_el ||
        (layout->level_offsets_B[l] % AIL_PAGESIZE)) return 0;
    tiles_x[l] = DIV_ROUND_UP(util_format_get_nblocksx(layout->format, u_minify(layout->width_px, l)),
                              2u * hw.width_el);
    tiles_y[l] = DIV_ROUND_UP(util_format_get_nblocksy(layout->format, u_minify(layout->height_px, l)),
                              2u * hw.height_el);
  }
  *standard_mips = packed;
  *packed_tiles = 0;
  if (packed < layout->levels) {
    if (layout->level_offsets_B[packed] % AIL_PAGESIZE) return 0;
    *packed_tiles = (unsigned)DIV_ROUND_UP(tail_bytes(layout, packed), AGX_WIN32_SPARSE_TILE_B);
  }
  return 1;
}

static void add_page(const struct ail_layout *layout, unsigned layer, uint64_t offset,
                     unsigned heap_page, AGX_WIN32_SPARSE_PAGES *pages) {
  const uint64_t layer_base = (uint64_t)layer * layout->layer_stride_B;
  pages->Offset[pages->Count] = offset;
  pages->HeapPage[pages->Count] = heap_page;
  pages->TableIndex[pages->Count] =
      ail_page_to_sparse_index_el(layout, layer, (unsigned)((offset - layer_base) / AIL_PAGESIZE));
  ++pages->Count;
}

int AgxWin32SparseStandardTile(const struct ail_layout *layout, unsigned layer, unsigned mip,
                               unsigned tile_x, unsigned tile_y, AGX_WIN32_SPARSE_PAGES *pages) {
  unsigned standard, packed, tx[AIL_MAX_MIP_LEVELS], ty[AIL_MAX_MIP_LEVELS];
  struct ail_tile hw;
  if (!pages || !AgxWin32SparseTiling(layout, &standard, tx, ty, &packed) ||
      !reservable(layout, &hw) || layer >= layout->depth_px || mip >= standard ||
      tile_x >= tx[mip] || tile_y >= ty[mip]) return 0;
  const unsigned w_el = util_format_get_nblocksx(layout->format, u_minify(layout->width_px, mip));
  const unsigned h_el = util_format_get_nblocksy(layout->format, u_minify(layout->height_px, mip));
  const unsigned bw = util_format_get_blockwidth(layout->format);
  const unsigned bh = util_format_get_blockheight(layout->format);
  pages->Count = 0;
  for (unsigned dy = 0; dy < 2u; ++dy)
    for (unsigned dx = 0; dx < 2u; ++dx) {
      const unsigned x_el = (2u * tile_x + dx) * hw.width_el;
      const unsigned y_el = (2u * tile_y + dy) * hw.height_el;
      /* a hardware tile past the mip's edge is not part of the image */
      if (x_el >= w_el || y_el >= h_el) continue;
      add_page(layout, layer, ail_get_twiddled_block_B(layout, mip, x_el * bw, y_el * bh, layer),
               dx + 2u * dy, pages);
    }
  return 1;
}

int AgxWin32SparsePackedTile(const struct ail_layout *layout, unsigned layer, unsigned tile,
                             AGX_WIN32_SPARSE_PAGES *pages) {
  unsigned standard, packed, tx[AIL_MAX_MIP_LEVELS], ty[AIL_MAX_MIP_LEVELS];
  if (!pages || !AgxWin32SparseTiling(layout, &standard, tx, ty, &packed) || !packed ||
      layer >= layout->depth_px || tile >= packed) return 0;
  const uint64_t start = (uint64_t)layer * layout->layer_stride_B + layout->level_offsets_B[standard];
  const uint64_t bytes = tail_bytes(layout, standard);
  pages->Count = 0;
  for (unsigned p = 0; p < 4u; ++p) {
    const uint64_t at = (uint64_t)tile * AGX_WIN32_SPARSE_TILE_B + (uint64_t)p * AIL_PAGESIZE;
    if (at < bytes) add_page(layout, layer, start + at, p, pages);
  }
  return 1;
}

uint32_t AgxWin32SparseBlock(uint64_t page_va, int enabled) {
  if (!enabled) return 0u;
  return (uint32_t)((page_va >> 14) & 0x3ffffffu) | (1u << 30) | (1u << 31);
}
