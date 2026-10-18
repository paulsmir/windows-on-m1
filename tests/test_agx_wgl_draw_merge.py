"""EXP1183: one merged draw draws exactly the primitives of the per-draw path.

Invariant, for random GL multi-draws (POLYGON/QUADS generated through
u_index_generator, TRIANGLE_FAN/TRIANGLE_STRIP/LINE_STRIP merged with
primitive restart), both provoking-vertex conventions, empty and degenerate
draws, and starts around the 16-bit index limit: the primitive sequence of
the merged draw (vertex tuples in order) equals the concatenation of the
sequences the driver drew per draw before -- u_primconvert's generator per
draw for non-native modes, the native fan/strip rule per draw otherwise; no
payload index equals the restart index; the merge never writes past the size
it reports.
"""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ICD = ROOT / 'drivers/apple-agx/windows/icd'
MESA = Path('/Users/pavel/public_windows/.local/reference/mesa')

PROGRAM = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "agx_wgl_draw_merge.h"
#include "indices/u_indices.h"
#include "util/u_prim.h"

typedef struct { unsigned start, count; int bias; } DRAW;
static unsigned rng = 1u;
static unsigned rnd(unsigned n) { rng = rng * 1103515245u + 12345u; return (rng >> 8) % n; }

/* Expand an index list into primitive tuples (3 per triangle, 2 per line). */
static unsigned expand(enum mesa_prim prim, const unsigned *ix, unsigned n, unsigned *out) {
  unsigned k = 0;
  if (prim == MESA_PRIM_TRIANGLES) { for (unsigned i = 0; i + 2 < n; i += 3) { out[k++] = ix[i]; out[k++] = ix[i+1]; out[k++] = ix[i+2]; } }
  else if (prim == MESA_PRIM_TRIANGLE_FAN) { for (unsigned i = 1; i + 1 < n; ++i) { out[k++] = ix[0]; out[k++] = ix[i]; out[k++] = ix[i+1]; } }
  else if (prim == MESA_PRIM_TRIANGLE_STRIP) { for (unsigned i = 0; i + 2 < n; ++i) {
      if (i & 1) { out[k++] = ix[i+1]; out[k++] = ix[i]; out[k++] = ix[i+2]; }
      else { out[k++] = ix[i]; out[k++] = ix[i+1]; out[k++] = ix[i+2]; } } }
  else if (prim == MESA_PRIM_LINE_STRIP) { for (unsigned i = 0; i + 1 < n; ++i) { out[k++] = ix[i]; out[k++] = ix[i+1]; } }
  else assert(!"prim");
  return k;
}

static unsigned load(const void *buf, unsigned size, unsigned i) {
  return size == 2 ? ((const unsigned short *)buf)[i] : ((const unsigned *)buf)[i];
}

int main(void) {
  /* Asahi's native mask: everything but QUADS, QUAD_STRIP and POLYGON. */
  unsigned hw = ((1u << MESA_PRIM_COUNT) - 1u) & ~((1u << MESA_PRIM_QUADS) |
                (1u << MESA_PRIM_QUAD_STRIP) | (1u << MESA_PRIM_POLYGON));
  const enum mesa_prim modes[] = {MESA_PRIM_POLYGON, MESA_PRIM_QUADS, MESA_PRIM_TRIANGLE_FAN,
                                  MESA_PRIM_TRIANGLE_STRIP, MESA_PRIM_LINE_STRIP};
  static unsigned want[1 << 20], got[1 << 20], ix[1 << 16];
  unsigned merged_cases = 0, wide = 0;
  for (unsigned c = 0; c < 20000; ++c) {
    enum mesa_prim mode = modes[rnd(5)];
    bool first = rnd(2);
    unsigned pv = first ? PV_FIRST : PV_LAST, n = 1 + rnd(24);
    DRAW d[32];
    unsigned base = rnd(8) == 0 ? 0xffe0u + rnd(64) : rnd(4000);
    for (unsigned i = 0; i < n; ++i) {
      d[i].start = base + rnd(200); d[i].count = rnd(13); d[i].bias = 0;
      if (rnd(7) == 0) d[i].count = 0;
    }
    assert(sizeof(DRAW) == 12);
    if (!agx_wgl_merge_applies(hw, mode, n)) {
      assert(n == 1 && mode != MESA_PRIM_POLYGON && mode != MESA_PRIM_QUADS);
      continue;
    }
    size_t bytes = agx_wgl_merge_bytes(hw, mode, first, d, sizeof(DRAW), n);
    unsigned char *buf = malloc(bytes + 64);
    memset(buf, 0xcd, bytes + 64);
    AGX_WGL_MERGED_DRAW m;
    bool any = agx_wgl_merge_build(hw, mode, first, d, sizeof(DRAW), n, buf, &m);
    for (unsigned g = 0; g < 64; ++g) assert(buf[bytes + g] == 0xcd);
    assert(!any || (size_t)m.count * m.index_size <= bytes);
    /* reference: the per-draw path */
    unsigned wk = 0;
    for (unsigned i = 0; i < n; ++i) {
      if (!d[i].count) continue;
      if (mode == MESA_PRIM_POLYGON || mode == MESA_PRIM_QUADS) {
        enum mesa_prim prim; unsigned size, nr; u_generate_func gen;
        unsigned count = d[i].count;
        if (!u_trim_pipe_prim(mode, &count)) continue;   /* as u_primconvert */
        u_index_generator(hw, mode, d[i].start, count, pv, pv, &prim, &size, &nr, &gen);
        if (!nr) continue;
        unsigned char tmp[4 * 64];
        gen(d[i].start, nr, tmp);
        for (unsigned j = 0; j < nr; ++j) ix[j] = load(tmp, size, j);
        wk += expand(prim, ix, nr, want + wk);
      } else {
        for (unsigned j = 0; j < d[i].count; ++j) ix[j] = d[i].start + j;
        wk += expand(mode, ix, d[i].count, want + wk);
      }
    }
    /* merged draw */
    unsigned gk = 0;
    if (any) {
      ++merged_cases; if (m.index_size == 4) ++wide;
      unsigned seg = 0, len = 0;
      for (unsigned i = 0; i <= m.count; ++i) {
        unsigned v = i < m.count ? load(buf, m.index_size, i) : 0;
        if (i == m.count || (m.restart && v == m.restart_index)) {
          gk += expand(m.prim, ix + seg, len, got + gk);
          seg = 0; len = 0;
          continue;
        }
        ix[seg + len++] = v;
      }
      if (m.restart) for (unsigned i = 0; i < m.count; ++i) {
        unsigned v = load(buf, m.index_size, i);
        assert(v == m.restart_index || v < m.restart_index);
      }
    }
    if (wk != gk || memcmp(want, got, wk * sizeof(unsigned))) {
      printf("MISMATCH case %u mode %d first %d n %u wk %u gk %u\n", c, mode, first, n, wk, gk);
      return 1;
    }
    free(buf);
  }
  assert(merged_cases > 10000 && wide > 100);
  printf("PASS %u merged, %u with 32-bit indices\n", merged_cases, wide);
  return 0;
}
'''


@unittest.skipUnless((MESA / 'src/gallium/auxiliary/indices/u_indices_gen.py').exists(),
                     'reference Mesa tree not present')
class DrawMerge(unittest.TestCase):
    def test_merged_draw_matches_per_draw_primitives(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            subprocess.run([sys.executable, '-I', str(MESA / 'src/gallium/auxiliary/indices/u_indices_gen.py'),
                            str(tmp / 'u_indices_gen.c')], check=True, cwd=tmp)
            (tmp / 't.c').write_text(PROGRAM)
            inc = ['-I' + str(ICD), '-I' + str(MESA / 'src'), '-I' + str(MESA / 'include'),
                   '-I' + str(MESA / 'src/gallium/include'), '-I' + str(MESA / 'src/gallium/auxiliary'),
                   '-I' + str(MESA / 'src/util')]
            subprocess.run(['cc', '-std=c11', '-O1', '-w', *inc, str(tmp / 'u_indices_gen.c'),
                            str(ICD / 'agx_wgl_draw_merge.c'), str(tmp / 't.c'), '-o', str(tmp / 't')],
                           check=True)
            out = subprocess.run([str(tmp / 't')], capture_output=True, text=True)
            self.assertEqual(out.returncode, 0, out.stdout + out.stderr)
            self.assertIn('PASS', out.stdout)


if __name__ == '__main__':
    unittest.main()
