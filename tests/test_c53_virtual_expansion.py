"""C53 B1: a submission naming a reserved (virtual) buffer holds its backing BOs.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A reserved buffer's VA maps
tiles of other BOs (D3D12 UpdateTileMappings). VidMm only keeps an allocation
resident for a submission that lists it, so a batch that reads or writes the
reserved range must list every distinct backing BO besides the anchor, and a
batch that writes it must mark those backing BOs written (the UMD refreshes
their CPU copies). Unmapped (scratch) tiles add nothing; duplicates are listed
once; the reference limit still refuses.
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
#include <string.h>
typedef struct { uint64_t Allocation, Va, Bytes; unsigned Bound; } AGX_WIN32_GPUVA_BO;
typedef struct { int Unused; } AGX_WIN32_ASAHI_BACKEND;
struct agx_bo { AGX_WIN32_GPUVA_BO View; unsigned Tiles; struct agx_bo *Tile[8]; };
#define AGX_G4_WRITTEN_LIMIT 4u
static const AGX_WIN32_GPUVA_BO *AgxWin32AsahiGpuvaBo(AGX_WIN32_ASAHI_BACKEND *b, struct agx_bo *bo) {
  (void)b; return bo && bo->View.Bound ? &bo->View : NULL; }
static unsigned AgxWin32AsahiVirtualTileCount(struct agx_bo *bo) { return bo ? bo->Tiles : 0; }
static struct agx_bo *AgxWin32AsahiVirtualTile(struct agx_bo *bo, unsigned t) { return bo->Tile[t]; }
@@FUNCS@@
static struct agx_bo bo(uint64_t allocation) { struct agx_bo b; memset(&b, 0, sizeof(b));
  b.View.Allocation = allocation; b.View.Bound = 1; return b; }
int main(void) {
  AGX_WIN32_ASAHI_BACKEND b = {0};
  struct agx_bo anchor = bo(10), heap_a = bo(20), heap_b = bo(30), plain = bo(40);
  /* tiles: A, scratch, B, A, scratch */
  anchor.Tiles = 5; anchor.Tile[0] = &heap_a; anchor.Tile[2] = &heap_b; anchor.Tile[3] = &heap_a;
  const AGX_WIN32_GPUVA_BO *refs[8]; unsigned count = 0;
  assert(add_bo(&b, refs, &count, 8, &plain) && count == 1);
  assert(add_bo(&b, refs, &count, 8, &anchor) && count == 4);
  assert(refs[1]->Allocation == 10 && refs[2]->Allocation == 20 && refs[3]->Allocation == 30);
  /* naming it again, or a backing directly, adds nothing */
  assert(add_bo(&b, refs, &count, 8, &anchor) && add_bo(&b, refs, &count, 8, &heap_b) && count == 4);
  /* the limit refuses the expansion too */
  count = 0;
  assert(!add_bo(&b, refs, &count, 2, &anchor));
  /* a written reserved buffer marks its backing BOs written */
  const AGX_WIN32_GPUVA_BO *written[AGX_G4_WRITTEN_LIMIT]; unsigned n = 0;
  assert(add_written(&b, written, &n, &anchor) && n == 3);
  assert(written[0]->Allocation == 10 && written[1]->Allocation == 20 && written[2]->Allocation == 30);
  assert(add_written(&b, written, &n, &heap_a) && n == 3);
  assert(add_written(&b, written, &n, &plain) && n == 4);
  assert(!add_written(&b, written, &n, &(struct agx_bo){ .View = {50, 0, 0, 1} }));
  puts("c53 virtual expansion: PASS");
  return 0;
}
'''


class C53VirtualExpansion(unittest.TestCase):
    def test_submissions_hold_and_mark_backing_bos(self):
        source = (ROOT / 'drivers/apple-agx/mesa/winsys/agx_win32_gpuva_batch.c').read_text()
        funcs = '\n'.join(body(source, name) for name in ('add_bo', 'add_written'))
        with tempfile.TemporaryDirectory(prefix='c53-virtual-') as tmp:
            src = Path(tmp) / 'expand.c'
            exe = Path(tmp) / 'expand'
            src.write_text(TEST.replace('@@FUNCS@@', funcs))
            built = subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                                    '-fsanitize=address,undefined', str(src), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('c53 virtual expansion: PASS', ran.stdout)


if __name__ == '__main__':
    unittest.main()
