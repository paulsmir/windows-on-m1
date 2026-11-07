"""C53 T2D: the 16 KiB pages and Sparse Block entries of reserved texture tiles.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A D3D12 standard tile of a
reserved Texture2D is 2x2 AGX hardware tiles of one page each, at the real
ail layout's twiddled-block offsets; heap pages follow Honeykrisp
bind_hw_tile (+1 odd column, +2 odd row); hardware tiles past a mip's edge
are not bound; packed mips are whole 64 KiB tiles of each layer's tail; the
Sparse Block index is ail_page_to_sparse_index_el of the page in its layer.
Any slip maps texels of one tile onto another tile's memory. Runs against
the pinned host build of libasahi_layout (real ail_make_miptree).
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from test_dxbc_to_nir import ROOT, local_tree, host_flags

TEST = r'''
#include "agx_win32_sparse.h"
#include "util/format/u_format.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define K 1024ull
static struct ail_layout make(enum pipe_format format, unsigned w, unsigned h, unsigned layers,
                              unsigned levels) {
  struct ail_layout l;
  memset(&l, 0, sizeof(l));
  l.width_px = w; l.height_px = h; l.depth_px = layers; l.sample_count_sa = 1;
  l.levels = levels; l.tiling = AIL_TILING_GPU; l.format = format;
  ail_make_miptree(&l);
  return l;
}
static int page(const AGX_WIN32_SPARSE_PAGES *p, unsigned i, unsigned long long offset, unsigned heap,
                unsigned table) {
  return p->Offset[i] == offset && p->HeapPage[i] == heap && p->TableIndex[i] == table;
}
int main(void) {
  unsigned std, tx[16], ty[16], packed;
  AGX_WIN32_SPARSE_PAGES p;
  /* 512x512 RGBA8, full chain: 64x64 hardware tiles, 128x128 standard tiles;
   * mips 0-2 standard (4x4, 2x2, 1x1), 64x64 and below packed */
  struct ail_layout a = make(PIPE_FORMAT_R8G8B8A8_UNORM, 512, 512, 1, 10);
  assert(AgxWin32SparseTiling(&a, &std, tx, ty, &packed));
  assert(std == 3 && tx[0] == 4 && ty[0] == 4 && tx[1] == 2 && tx[2] == 1 && ty[2] == 1);
  assert(packed == (a.level_offsets_B[10] - a.level_offsets_B[3] + 64 * K - 1) / (64 * K));
  assert(packed == 1);
  /* standard tile (1,0) of mip 0: hardware tiles (2,0) (3,0) (2,1) (3,1), 8 per row */
  assert(AgxWin32SparseStandardTile(&a, 0, 0, 1, 0, &p) && p.Count == 4);
  assert(page(&p, 0, 32 * K, 0, 2) && page(&p, 1, 48 * K, 1, 3) && page(&p, 2, 160 * K, 2, 10) &&
         page(&p, 3, 176 * K, 3, 11));
  /* mip 1 starts at 1 MiB, 4 hardware tiles per row */
  assert(AgxWin32SparseStandardTile(&a, 0, 1, 1, 1, &p) && p.Count == 4);
  assert(page(&p, 0, 1024 * K + 10 * 16 * K, 0, 74) && page(&p, 3, 1024 * K + 15 * 16 * K, 3, 79));
  /* the packed tile: the pages of the tail only */
  assert(AgxWin32SparsePackedTile(&a, 0, 0, &p) && p.Count == 2);
  assert(page(&p, 0, a.level_offsets_B[3], 0, (unsigned)(a.level_offsets_B[3] / (16 * K))) &&
         page(&p, 1, a.level_offsets_B[3] + 16 * K, 1, (unsigned)(a.level_offsets_B[3] / (16 * K)) + 1));
  assert(!AgxWin32SparsePackedTile(&a, 0, 1, &p) && !AgxWin32SparseStandardTile(&a, 0, 0, 4, 0, &p) &&
         !AgxWin32SparseStandardTile(&a, 0, 3, 0, 0, &p) && !AgxWin32SparseStandardTile(&a, 1, 0, 0, 0, &p));
  /* 192x192, one mip: 3x3 hardware tiles, so standard tile (1,1) holds one */
  struct ail_layout b = make(PIPE_FORMAT_R8G8B8A8_UNORM, 192, 192, 1, 1);
  assert(AgxWin32SparseTiling(&b, &std, tx, ty, &packed) && std == 1 && tx[0] == 2 && !packed);
  assert(AgxWin32SparseStandardTile(&b, 0, 0, 1, 1, &p) && p.Count == 1 && page(&p, 0, 8 * 16 * K, 0, 8));
  assert(!AgxWin32SparsePackedTile(&b, 0, 0, &p));
  /* 64 bpp: 64x32 hardware tiles, 128x64 standard */
  struct ail_layout c = make(PIPE_FORMAT_R16G16B16A16_FLOAT, 256, 256, 1, 1);
  assert(AgxWin32SparseTiling(&c, &std, tx, ty, &packed) && tx[0] == 2 && ty[0] == 4);
  /* BC1 (8-byte 4x4 blocks): standard tile 512x256 texels */
  struct ail_layout d = make(PIPE_FORMAT_DXT1_RGBA, 1024, 1024, 1, 1);
  assert(AgxWin32SparseTiling(&d, &std, tx, ty, &packed) && tx[0] == 2 && ty[0] == 4);
  /* an array: layer 1's pages and table entries are its own */
  struct ail_layout e = make(PIPE_FORMAT_R8G8B8A8_UNORM, 256, 256, 2, 5);
  assert(e.layer_stride_B % (16 * K) == 0);
  assert(AgxWin32SparseTiling(&e, &std, tx, ty, &packed) && std == 2 && packed == 1);
  assert(AgxWin32SparseStandardTile(&e, 1, 0, 0, 0, &p) && p.Count == 4);
  assert(page(&p, 0, e.layer_stride_B, 0, e.sparse_folios_per_layer * 512));
  assert(AgxWin32SparsePackedTile(&e, 1, 0, &p) &&
         p.Offset[0] == e.layer_stride_B + e.level_offsets_B[2] &&
         p.TableIndex[0] == e.sparse_folios_per_layer * 512 + (unsigned)(e.level_offsets_B[2] / (16 * K)));
  /* layouts that cannot be reserved */
  struct ail_layout f;
  memset(&f, 0, sizeof(f));
  f.width_px = 256; f.height_px = 256; f.depth_px = 1; f.sample_count_sa = 4;
  f.levels = 1; f.tiling = AIL_TILING_GPU; f.format = PIPE_FORMAT_R8G8B8A8_UNORM;
  ail_make_miptree(&f);
  assert(!AgxWin32SparseTiling(&f, &std, tx, ty, &packed));
  struct ail_layout g = make(PIPE_FORMAT_R8G8B8A8_UNORM, 256, 256, 1, 1);
  g.tiling = AIL_TILING_LINEAR;
  assert(!AgxWin32SparseTiling(&g, &std, tx, ty, &packed));
  /* Sparse Blocks */
  assert(AgxWin32SparseBlock(0x123456789ull << 14, 0) == 0u);
  assert(AgxWin32SparseBlock(0x12345ull << 14, 1) == (0x12345u | (1u << 30) | (1u << 31)));
  puts("c53 sparse pages: PASS");
  return 0;
}
'''


class C53SparsePages(unittest.TestCase):
    def test_tile_pages_on_the_real_ail_layout(self):
        mesa = local_tree('reference', 'mesa')
        build = local_tree('accelerated-desktop-ad03', 'mesa-build')
        if not mesa or not build:
            self.skipTest('pinned Mesa/host AIL unavailable')
        winsys = ROOT / 'drivers/apple-agx/mesa/winsys'
        libs = [build / p for p in ('src/asahi/layout/libasahi_layout.a', 'src/util/libmesa_util.a',
                                    'src/util/libmesa_util_simd.a', 'src/c11/impl/libmesa_util_c11.a',
                                    'src/util/blake3/libblake3.a')]
        with tempfile.TemporaryDirectory(prefix='c53-sparse-') as tmp:
            src = Path(tmp) / 'sparse.c'
            exe = Path(tmp) / 'sparse'
            src.write_text(TEST)
            built = subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                                    '-Wno-unused-parameter', '-fsanitize=address,undefined', *host_flags(build),
                                    '-I', str(mesa / 'src'), '-I', str(winsys), str(src),
                                    str(winsys / 'agx_win32_sparse.c'), '-o', str(exe), *map(str, libs),
                                    '-L/opt/homebrew/opt/zstd/lib', '-lzstd', '-lz', '-lpthread'],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr[-4000:])
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('c53 sparse pages: PASS', ran.stdout)


if __name__ == '__main__':
    unittest.main()
