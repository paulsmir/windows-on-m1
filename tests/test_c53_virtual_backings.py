"""C53 B4: a virtual BO's backing list and tile-scratch runs.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A reserved resource holds
exactly one reference to each distinct BO its tiles map (the tile scratch
included), with a tile count, and VirtualBackingRefs counts those entries,
which bounds a submission's expansion. A wrong count leaks a heap (never
freed) or drops it while tiles still map it (a GPU read of freed memory); a
remap onto the same heap must not let the reference lapse; a refused map
must change nothing. Unmapped runs map the scratch in runs of at most 16
tiles, one map each.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from g3_vidmm_replay import body

ROOT = Path(__file__).resolve().parents[1]

TEST = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { uint64_t Allocation, Va, Bytes; unsigned Bound; } AGX_WIN32_GPUVA_BO;
struct agx_bo { int refcnt; };
typedef struct AGX_WIN32_ASAHI_BACKEND AGX_WIN32_ASAHI_BACKEND;
struct windows_bo {
  struct agx_bo Base; AGX_WIN32_ASAHI_BACKEND *Backend; AGX_WIN32_GPUVA_BO Gpuva;
  int Virtual; AGX_WIN32_GPUVA_BO Reserved; struct windows_bo **Tiles; unsigned TileCount;
  struct { struct windows_bo *Bo; unsigned Tiles; } *Backings; unsigned BackingCount;
};
struct AGX_WIN32_ASAHI_BACKEND {
  int Failed; unsigned VirtualBackingRefs; void *Native; struct windows_bo *Scratch;
  int Gpuva; int MapOk; unsigned Maps; uint64_t LastBytes;
};
static AGX_WIN32_ASAHI_BACKEND g;
#define AGX_WIN32_TILE 0x10000u
#define AGX_WIN32_TILE_SCRATCH_TILES 16u
#define AGX_GPUVA_MAP_WRITE 1u
#define AGX_WIN32_ASAHI_FAIL(b, x) ((b)->Failed = (int)(x))
static void agx_bo_reference(struct agx_bo *bo) { ++bo->refcnt; }
static void agx_bo_unreference(void *dev, struct agx_bo *bo) { (void)dev; --bo->refcnt; }
static struct windows_bo *virtual_scratch(AGX_WIN32_ASAHI_BACKEND *b) { return b->Scratch; }
static int virtual_retire(AGX_WIN32_ASAHI_BACKEND *b, struct windows_bo *bo) { (void)b; (void)bo; return 1; }
static int AgxWin32GpuvaMapRange(int *space, uint64_t va, uint64_t allocation, uint64_t offset,
                                 uint64_t bytes, unsigned protection) {
  (void)space; (void)va; (void)allocation; (void)offset; (void)protection;
  if (!g.MapOk) return 0;
  ++g.Maps; g.LastBytes = bytes; return 1;
}
@@FUNCS@@
static struct windows_bo heap(uint64_t allocation, uint64_t tiles) {
  struct windows_bo h; memset(&h, 0, sizeof(h));
  h.Base.refcnt = 1; h.Backend = &g; h.Gpuva.Allocation = allocation; h.Gpuva.Bound = 1;
  h.Gpuva.Bytes = tiles * AGX_WIN32_TILE; return h;
}
static unsigned tiles_of(struct windows_bo *v, struct windows_bo *b) {
  for (unsigned i = 0; i < v->BackingCount; ++i) if (v->Backings[i].Bo == b) return v->Backings[i].Tiles;
  return 0;
}
int main(void) {
  struct windows_bo scratch = heap(60, 16), a = heap(20, 8), b = heap(30, 8);
  struct windows_bo v = heap(10, 1);
  g.Scratch = &scratch; g.MapOk = 1;
  v.Virtual = 1; v.TileCount = 40; v.Reserved.Va = 0x1000000; v.Reserved.Bound = 1;
  v.Tiles = calloc(v.TileCount, sizeof(*v.Tiles));
  for (unsigned t = 0; t < v.TileCount; ++t) v.Tiles[t] = &scratch;
  /* creation: one scratch entry for every tile, mapped in runs of 16 */
  assert(backing_add(&g, &v, &scratch, v.TileCount) && map_scratch(&g, &scratch, v.Reserved.Va, 40));
  assert(g.Maps == 3 && g.LastBytes == 8 * AGX_WIN32_TILE);
  assert(scratch.Base.refcnt == 2 && g.VirtualBackingRefs == 1 && tiles_of(&v, &scratch) == 40);
  /* tiles 1-2 onto heap A: one reference for A, counts move */
  g.Maps = 0;
  assert(AgxWin32AsahiMapVirtualTiles(&v.Base, 1, 2, &a.Base, 0) && g.Maps == 1);
  assert(a.Base.refcnt == 2 && g.VirtualBackingRefs == 2);
  assert(tiles_of(&v, &a) == 2 && tiles_of(&v, &scratch) == 38 && v.Tiles[2] == &a);
  /* remap tile 2 onto another tile of A: A's reference never lapses */
  assert(AgxWin32AsahiMapVirtualTiles(&v.Base, 2, 1, &a.Base, 5));
  assert(a.Base.refcnt == 2 && tiles_of(&v, &a) == 2 && g.VirtualBackingRefs == 2);
  /* a refused map changes nothing */
  g.MapOk = 0;
  assert(!AgxWin32AsahiMapVirtualTiles(&v.Base, 0, 4, &b.Base, 0));
  assert(b.Base.refcnt == 1 && tiles_of(&v, &b) == 0 && tiles_of(&v, &a) == 2 &&
         v.Tiles[0] == &scratch && g.VirtualBackingRefs == 2);
  g.MapOk = 1;
  /* ranges outside the resource or the heap are refused before any change */
  assert(!AgxWin32AsahiMapVirtualTiles(&v.Base, 39, 2, NULL, 0));
  assert(!AgxWin32AsahiMapVirtualTiles(&v.Base, 0, 9, &b.Base, 0) && b.Base.refcnt == 1);
  /* tiles 0-3 onto B, then all 40 back to scratch: every heap reference is
   * dropped, scratch holds all tiles again, in 3 scratch runs */
  assert(AgxWin32AsahiMapVirtualTiles(&v.Base, 0, 4, &b.Base, 4));
  assert(a.Base.refcnt == 1 && b.Base.refcnt == 2 && tiles_of(&v, &b) == 4 && tiles_of(&v, &a) == 0);
  g.Maps = 0;
  assert(AgxWin32AsahiMapVirtualTiles(&v.Base, 0, 40, NULL, 0) && g.Maps == 3);
  assert(b.Base.refcnt == 1 && g.VirtualBackingRefs == 1 && tiles_of(&v, &scratch) == 40 &&
         scratch.Base.refcnt == 2 && v.BackingCount == 1);
  free(v.Tiles); free(v.Backings);
  puts("c53 virtual backings: PASS");
  return 0;
}
'''


class C53VirtualBackings(unittest.TestCase):
    def test_backing_references_follow_the_tiles(self):
        source = (ROOT / 'drivers/apple-agx/mesa/winsys/agx_win32_asahi_bo.c').read_text()
        funcs = '\n'.join(body(source, name) for name in
                          ('map_scratch', 'backing_add', 'backing_drop', 'AgxWin32AsahiMapVirtualTiles'))
        with tempfile.TemporaryDirectory(prefix='c53-backings-') as tmp:
            src = Path(tmp) / 'backings.c'
            exe = Path(tmp) / 'backings'
            src.write_text(TEST.replace('@@FUNCS@@', funcs))
            built = subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                                    '-fsanitize=address,undefined', str(src), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('c53 virtual backings: PASS', ran.stdout)


if __name__ == '__main__':
    unittest.main()
