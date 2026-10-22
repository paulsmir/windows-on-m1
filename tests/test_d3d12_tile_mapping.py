"""C53 B2: the UpdateTileMappings walk for reserved buffers.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? D3D12 UpdateTileMappings
walks resource regions and tile ranges together with several defaults
(NULL starts, sizes, flags and counts). A wrong default or an off-by-one at a
region/range boundary maps a resource tile to the wrong heap tile: silent
corruption of a reserved buffer. The cases are the four examples of
ID3D12CommandQueue::UpdateTileMappings (Microsoft Learn) laid out along X,
plus the malformed requests that must map nothing.
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
  puts("tile mapping walk: PASS");
  return 0;
}
'''


class TileMappingWalk(unittest.TestCase):
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
