"""C53 B2: the UpdateTileMappings walk for reserved buffers.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? D3D12 UpdateTileMappings
walks resource regions and tile ranges together with several defaults
(NULL starts, sizes, flags and counts). A wrong default or an off-by-one at a
region/range boundary maps a resource tile to the wrong heap tile: silent
corruption of a reserved buffer. The cases are the four examples of
ID3D12CommandQueue::UpdateTileMappings (Microsoft Learn) laid out along X,
plus the malformed requests that must map nothing. C53 T2D: texture tile
order (per slice: standard mips row by row, then the shared packed tiles),
boxes as rows inside one standard mip, and the reverse lookup the bind uses
-- an off-by-one there binds the wrong 16 KiB pages of a reserved texture.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

TEST = r'''
#include "agx_d3d12_tile_mapping.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static AGX_TILE_RUN runs[32]; static unsigned n;
static int emit(void *c, const AGX_TILE_RUN *r) { (void)c; runs[n++] = *r; return 1; }
static int run_is(unsigned i, unsigned tile, unsigned tiles, unsigned kind, unsigned heap) {
  return runs[i].ResourceTile == tile && runs[i].Tiles == tiles && runs[i].Kind == kind &&
         (kind == AGX_TILE_RANGE_NULL || runs[i].HeapTile == heap); }
int main(void) {
  /* clearing every mapping to NULL */
  unsigned null_flag = AGX_TILE_RANGE_NULL;
  n = 0; assert(AgxTileMappingWalk(40, 1, NULL, NULL, 1, &null_flag, NULL, NULL, 0, emit, 0));
  assert(n == 1 && run_is(0, 0, 40, AGX_TILE_RANGE_NULL, 0));
  /* a 6-tile region at tile 1 onto the single heap tile 12 */
  AGX_TILE_COORD at1 = {1, 0, 0, 0}; AGX_TILE_REGION six = {6, 1, 6, 1, 1};
  unsigned reuse = AGX_TILE_RANGE_REUSE, twelve = 12;
  n = 0; assert(AgxTileMappingWalk(40, 1, &at1, &six, 1, &reuse, &twelve, NULL, 16, emit, 0));
  assert(n == 1 && run_is(0, 1, 6, AGX_TILE_RANGE_REUSE, 12));
  /* disjoint single tiles: no sizes, no flags, no counts (one tile each) */
  AGX_TILE_COORD tiles[3] = {{1, 0, 0, 0}, {4, 0, 0, 0}, {2, 0, 0, 0}};
  unsigned starts[3] = {1, 4, 7};
  n = 0; assert(AgxTileMappingWalk(40, 3, tiles, NULL, 3, NULL, starts, NULL, 16, emit, 0));
  assert(n == 3 && run_is(0, 1, 1, 0, 1) && run_is(1, 4, 1, 0, 4) && run_is(2, 2, 1, 0, 7));
  /* the complex example: 22 tiles in regions of 6, 9 and 7 */
  AGX_TILE_COORD c[3] = {{1, 0, 0, 0}, {10, 0, 0, 0}, {20, 0, 0, 0}};
  AGX_TILE_REGION s[3] = {{6, 0, 0, 0, 0}, {9, 1, 9, 1, 1}, {7, 0, 0, 0, 0}};
  unsigned f[8] = {0, AGX_TILE_RANGE_SKIP, AGX_TILE_RANGE_NULL, AGX_TILE_RANGE_REUSE, 0, 0, 0, 0};
  unsigned o[8] = {9, 0, 0, 17, 2, 9, 4, 17}, k[8] = {3, 8, 2, 5, 1, 1, 1, 1};
  n = 0; assert(AgxTileMappingWalk(40, 3, c, s, 8, f, o, k, 18, emit, 0));
  assert(n == 8);
  assert(run_is(0, 1, 3, 0, 9));                         /* region 0 tiles 1-3 -> heap 9-11 */
  assert(run_is(1, 15, 2, AGX_TILE_RANGE_NULL, 0));      /* skipped 4-6 and 10-14 */
  assert(run_is(2, 17, 2, AGX_TILE_RANGE_REUSE, 17));    /* reuse crosses into region 2 */
  assert(run_is(3, 20, 3, AGX_TILE_RANGE_REUSE, 17));
  assert(run_is(4, 23, 1, 0, 2) && run_is(5, 24, 1, 0, 9) && run_is(6, 25, 1, 0, 4) && run_is(7, 26, 1, 0, 17));
  /* malformed requests map nothing */
  unsigned three = 3, heap0 = 0;
  n = 0; assert(!AgxTileMappingWalk(40, 1, &at1, &six, 1, NULL, &heap0, &three, 16, emit, 0) && n == 0); /* 6 != 3 */
  AGX_TILE_COORD far = {38, 0, 0, 0};
  n = 0; assert(!AgxTileMappingWalk(40, 1, &far, &six, 1, &null_flag, NULL, NULL, 0, emit, 0) && n == 0);
  unsigned fifteen = 15;
  n = 0; assert(!AgxTileMappingWalk(40, 1, &at1, &six, 1, NULL, &fifteen, NULL, 16, emit, 0) && n == 0); /* heap 15-20 */
  AGX_TILE_REGION tall = {6, 1, 3, 2, 1};
  n = 0; assert(!AgxTileMappingWalk(40, 1, &at1, &tall, 1, &null_flag, NULL, NULL, 0, emit, 0) && n == 0);
  n = 0; assert(!AgxTileMappingWalk(40, 1, &at1, &six, 1, NULL, NULL, NULL, 0, emit, 0) && n == 0); /* heap run, no heap */
  /* C53 T2D: a 512x512 32 bpp Texture2D array of 2 slices, 5 mips: standard
   * tiles 4x4, 2x2, 1x1, then mips 3-4 packed into 1 tile per slice */
  AGX_TILE_GEOMETRY tex = {5, 2, 3, 1, {4, 2, 1}, {4, 2, 1}};
  unsigned long long ix = 0;
  assert(AgxTileSliceTiles(&tex) == 22 && AgxTileResourceTiles(&tex) == 44);
  AGX_TILE_COORD c0 = {1, 2, 0, 0}, c1 = {1, 1, 0, 1}, cp3 = {0, 0, 0, 3}, cp4 = {0, 0, 0, 4};
  AGX_TILE_COORD s1 = {0, 0, 0, 5}, s1p = {0, 0, 0, 9};
  assert(AgxTileIndex(&tex, &c0, &ix) && ix == 9);
  assert(AgxTileIndex(&tex, &c1, &ix) && ix == 19);
  /* every packed mip of a slice names the same tiles */
  assert(AgxTileIndex(&tex, &cp3, &ix) && ix == 21 && AgxTileIndex(&tex, &cp4, &ix) && ix == 21);
  assert(AgxTileIndex(&tex, &s1, &ix) && ix == 22 && AgxTileIndex(&tex, &s1p, &ix) && ix == 43);
  AGX_TILE_COORD bad[4] = {{4, 0, 0, 0}, {1, 0, 0, 3}, {0, 0, 0, 10}, {0, 0, 1, 0}};
  for (unsigned i = 0; i < 4; ++i) assert(!AgxTileIndex(&tex, &bad[i], &ix));
  unsigned sl, mp, tx, ty; int pk;
  assert(AgxTileLocate(&tex, 9, &sl, &mp, &tx, &ty, &pk) && sl == 0 && mp == 0 && tx == 1 && ty == 2 && !pk);
  assert(AgxTileLocate(&tex, 19, &sl, &mp, &tx, &ty, &pk) && mp == 1 && tx == 1 && ty == 1 && !pk);
  assert(AgxTileLocate(&tex, 21, &sl, &mp, &tx, &ty, &pk) && sl == 0 && pk && tx == 0);
  assert(AgxTileLocate(&tex, 43, &sl, &mp, &tx, &ty, &pk) && sl == 1 && pk);
  assert(!AgxTileLocate(&tex, 44, &sl, &mp, &tx, &ty, &pk));
  /* a 2x2 box at (1,1) of mip 0: one run per row, heap tiles in box order */
  AGX_TILE_COORD b11 = {1, 1, 0, 0}; AGX_TILE_REGION box22 = {4, 1, 2, 2, 1}; unsigned zero = 0;
  n = 0; assert(AgxTileMappingWalkGeometry(&tex, 1, &b11, &box22, 1, NULL, &zero, NULL, 8, emit, 0));
  assert(n == 2 && run_is(0, 5, 2, 0, 0) && run_is(1, 9, 2, 0, 2));
  /* a range ending inside a row splits the run there */
  AGX_TILE_COORD b00 = {0, 0, 0, 0}; AGX_TILE_REGION box32 = {6, 1, 3, 2, 1};
  unsigned two_starts[2] = {0, 6}, two_counts[2] = {2, 4};
  n = 0; assert(AgxTileMappingWalkGeometry(&tex, 1, &b00, &box32, 2, NULL, two_starts, two_counts, 16, emit, 0));
  assert(n == 3 && run_is(0, 0, 2, 0, 0) && run_is(1, 2, 1, 0, 6) && run_is(2, 4, 3, 0, 7));
  /* without a box, NumTiles tiles in resource order, across mips */
  AGX_TILE_COORD c33 = {3, 3, 0, 0}; AGX_TILE_REGION three_tiles = {3, 0, 0, 0, 0};
  n = 0; assert(AgxTileMappingWalkGeometry(&tex, 1, &c33, &three_tiles, 1, &null_flag, NULL, NULL, 0, emit, 0));
  assert(n == 1 && run_is(0, 15, 3, AGX_TILE_RANGE_NULL, 0));
  /* the packed tile of slice 1 */
  AGX_TILE_REGION one = {1, 0, 0, 0, 0}; unsigned heap7 = 7;
  n = 0; assert(AgxTileMappingWalkGeometry(&tex, 1, &s1p, &one, 1, NULL, &heap7, NULL, 8, emit, 0));
  assert(n == 1 && run_is(0, 43, 1, 0, 7));
  /* malformed boxes: on a packed mip, deeper than 1, outside the mip, a
   * NumTiles that is not Width x Height; an inconsistent geometry */
  AGX_TILE_REGION boxp = {1, 1, 1, 1, 1}, deep = {2, 1, 1, 1, 2}, wide = {4, 1, 4, 1, 1}, odd = {3, 1, 2, 2, 1};
  n = 0; assert(!AgxTileMappingWalkGeometry(&tex, 1, &cp3, &boxp, 1, &null_flag, NULL, NULL, 0, emit, 0) && !n);
  assert(!AgxTileMappingWalkGeometry(&tex, 1, &b00, &deep, 1, &null_flag, NULL, NULL, 0, emit, 0) && !n);
  assert(!AgxTileMappingWalkGeometry(&tex, 1, &b11, &wide, 1, &null_flag, NULL, NULL, 0, emit, 0) && !n);
  assert(!AgxTileMappingWalkGeometry(&tex, 1, &b00, &odd, 1, &null_flag, NULL, NULL, 0, emit, 0) && !n);
  AGX_TILE_REGION past = {2, 0, 0, 0, 0};
  assert(!AgxTileMappingWalkGeometry(&tex, 1, &s1p, &past, 1, &null_flag, NULL, NULL, 0, emit, 0) && !n);
  AGX_TILE_GEOMETRY broken = {5, 1, 3, 0, {4, 2, 1}, {4, 2, 1}};
  assert(!AgxTileResourceTiles(&broken) &&
         !AgxTileMappingWalkGeometry(&broken, 1, NULL, NULL, 1, &null_flag, NULL, NULL, 0, emit, 0));
  /* C53 T2D: tile k of a region (CopyTileMappings) */
  unsigned long long rt = 0;
  assert(AgxTileRegionTile(&tex, &b11, &box22, 0, &rt) && rt == 5 && AgxTileRegionTile(&tex, &b11, &box22, 3, &rt) && rt == 10);
  assert(!AgxTileRegionTile(&tex, &b11, &box22, 4, &rt) && !AgxTileRegionTile(&tex, &b11, &wide, 0, &rt));
  assert(AgxTileRegionTile(&tex, &c33, &three_tiles, 2, &rt) && rt == 17);
  assert(AgxTileRegionTile(&tex, &s1p, NULL, 0, &rt) && rt == 43 && !AgxTileRegionTile(&tex, &s1p, &past, 0, &rt));
  puts("tile mapping walk: PASS");
  return 0;
}
'''


class TileMappingWalk(unittest.TestCase):
    def test_reserved_resources_need_no_runtime_heap(self):
        # WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? EXP1899: through
        # CreateReservedResource the runtime calls CreateHeapAndResource with
        # neither a heap description nor a heap handle (receipt
        # measure-d3d12-create-heap-resource flags 2). The reserved-buffer
        # branch required a heap handle, fell through to the placed-buffer
        # path (measure-d3d12-placed reason 1) and the device was removed
        # (0x887a0005). Reserved buffers and textures must both keep their
        # tile state in the resource's own heap when the runtime gives none.
        ddi = (ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp').read_text()
        create = ddi[ddi.index('static HRESULT APIENTRY CreateHeapAndResource('):]
        create = create[:create.index('\n}\n')]
        self.assertIn('return CreateReservedBuffer(device, heap ? heap : &resource->own_heap, resource_arg, resource);', create)
        self.assertIn('return CreateReservedTexture(device, heap ? heap : &resource->own_heap, resource_arg, resource);', create)
        reserved = create[create.index('/* C53 B3 [H]'):create.index('return CreateReservedBuffer(')]
        self.assertNotIn('&& heap &&', reserved)

    def test_update_tile_mappings_walk(self):
        with tempfile.TemporaryDirectory(prefix='tile-mapping-') as tmp:
            src = Path(tmp) / 'walk.c'
            exe = Path(tmp) / 'walk'
            src.write_text(TEST)
            built = subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                                    '-fsanitize=address,undefined', '-I', str(ROOT / 'drivers/apple-agx/d3d12'),
                                    str(src), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('tile mapping walk: PASS', ran.stdout)


if __name__ == '__main__':
    unittest.main()
