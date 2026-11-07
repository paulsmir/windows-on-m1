"""FL 11_1 forced sample count: the reference coverage (dxbc_tir.h).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? D3D rasterization covers
each sample exactly once across triangles that share an edge (the top-left
rule), whatever the winding. A wrong orientation fix-up or fill-rule bias
counts samples on a shared edge twice (UAV-only rendering then double-adds)
or never. The forced patterns must be the D3D standard ones. The GS and
pixel shader variant implement this reference; the hardware probe D11TIR
compares them against it.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

TEST = r'''
#include "dxbc_tir.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned tri(const int32_t *x, const int32_t *y, int px, int py, unsigned n) {
  struct dxbc_tir_edges e;
  return dxbc_tir_setup(x, y, &e) ? dxbc_tir_mask(&e, px, py, n) : 0u; }
/* the samples of pixel (px, py) inside [x0, x1) x [y0, y1) (1/256 px) */
static unsigned box(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int px, int py, unsigned n) {
  const int8_t (*p)[2] = dxbc_tir_pattern(n); unsigned m = 0;
  for (unsigned s = 0; s < n; ++s) {
    int32_t sx = px * 256 + 128 + p[s][0] * 16, sy = py * 256 + 128 + p[s][1] * 16;
    if (sx >= x0 && sx < x1 && sy >= y0 && sy < y1) m |= 1u << s; }
  return m; }
int main(void) {
  /* the patterns: distinct positions inside the pixel */
  for (unsigned n = 4; n <= 16; n *= 2) {
    const int8_t (*p)[2] = dxbc_tir_pattern(n);
    for (unsigned i = 0; i < n; ++i) {
      assert(p[i][0] >= -8 && p[i][0] < 8 && p[i][1] >= -8 && p[i][1] < 8);
      for (unsigned j = 0; j < i; ++j) assert(p[i][0] != p[j][0] || p[i][1] != p[j][1]); } }
  assert(!dxbc_tir_pattern(2) && !dxbc_tir_pattern(1));
  /* an axis-aligned quad split along either diagonal, either winding, edges
   * on sample rows: the halves are disjoint and their union is the quad */
  srand(7);
  for (int iter = 0; iter < 400; ++iter) {
    int32_t x0 = (rand() % 64) * 16, y0 = (rand() % 64) * 16;
    int32_t x1 = x0 + 16 + (rand() % 96) * 16, y1 = y0 + 16 + (rand() % 96) * 16;
    if (iter & 1) { x0 -= rand() % 256; y1 += rand() % 256; } /* off the sample grid too */
    int32_t qx[4] = {x0, x1, x1, x0}, qy[4] = {y0, y0, y1, y1};
    for (unsigned diag = 0; diag < 2; ++diag)
      for (unsigned wind = 0; wind < 2; ++wind) {
        unsigned a = diag, b = diag + 1, c = diag + 2, d = (diag + 3) & 3;
        int32_t t1x[3] = {qx[a], qx[b], qx[c]}, t1y[3] = {qy[a], qy[b], qy[c]};
        int32_t t2x[3] = {qx[a], qx[c], qx[d]}, t2y[3] = {qy[a], qy[c], qy[d]};
        if (wind) { int32_t s = t1x[1]; t1x[1] = t1x[2]; t1x[2] = s; s = t1y[1]; t1y[1] = t1y[2]; t1y[2] = s; }
        for (int py = y0 / 256 - 1; py <= y1 / 256 + 1; ++py)
          for (int px = x0 / 256 - 1; px <= x1 / 256 + 1; ++px)
            for (unsigned n = 4; n <= 16; n *= 2) {
              unsigned m1 = tri(t1x, t1y, px, py, n), m2 = tri(t2x, t2y, px, py, n);
              if ((m1 & m2) || (m1 | m2) != box(x0, y0, x1, y1, px, py, n)) {
                printf("FAIL quad %d,%d-%d,%d diag %u wind %u pixel %d,%d n %u: %x %x want %x\n", x0, y0, x1, y1,
                       diag, wind, px, py, n, m1, m2, box(x0, y0, x1, y1, px, py, n));
                return 1; } } }
  }
  /* zero area draws nothing; a full-pixel cover sets every sample */
  { int32_t x[3] = {0, 256, 512}, y[3] = {0, 256, 512}; assert(tri(x, y, 0, 0, 16) == 0); }
  { int32_t x[3] = {-4096, 8192, -4096}, y[3] = {-4096, -4096, 8192}; assert(tri(x, y, 0, 0, 16) == 0xffff);
    assert(tri(x, y, 0, 0, 8) == 0xff); assert(tri(x, y, 0, 0, 4) == 0xf); }
  /* conservative rasterization: touched == the square meets the closed
   * triangle (polygon clip in doubles), and touched >= any 16x coverage */
  for (int iter = 0; iter < 3000; ++iter) {
    int32_t x[3], y[3];
    for (int k = 0; k < 3; ++k) { x[k] = rand() % (24 * 256); y[k] = rand() % (24 * 256); }
    struct dxbc_tir_edges e;
    if (!dxbc_tir_setup(x, y, &e)) continue;
    for (int py = 0; py < 24; ++py)
      for (int px = 0; px < 24; ++px) {
        double poly[16][2], next[16][2]; int n = 4, m;
        double sx0 = px * 256.0, sy0 = py * 256.0;
        poly[0][0] = sx0; poly[0][1] = sy0; poly[1][0] = sx0 + 256; poly[1][1] = sy0;
        poly[2][0] = sx0 + 256; poly[2][1] = sy0 + 256; poly[3][0] = sx0; poly[3][1] = sy0 + 256;
        for (int i = 0; i < 3 && n; ++i) {
          m = 0;
          for (int k = 0; k < n; ++k) {
            const double *pa = poly[k], *pb = poly[(k + 1) % n];
            double ea = (double)e.a[i] * (pa[0] - e.x[i]) + (double)e.b[i] * (pa[1] - e.y[i]);
            double eb = (double)e.a[i] * (pb[0] - e.x[i]) + (double)e.b[i] * (pb[1] - e.y[i]);
            if (ea >= 0) { next[m][0] = pa[0]; next[m][1] = pa[1]; ++m; }
            if ((ea >= 0) != (eb >= 0)) { double s = ea / (ea - eb);
              next[m][0] = pa[0] + s * (pb[0] - pa[0]); next[m][1] = pa[1] + s * (pb[1] - pa[1]); ++m; }
          }
          n = m; memcpy(poly, next, sizeof(poly));
        }
        int meets = n > 0, touched = dxbc_tir_touched(&e, px, py);
        if (dxbc_tir_mask(&e, px, py, 16) && !touched) { printf("FAIL covered but not touched %d,%d\n", px, py); return 1; }
        if (meets != touched) {
          /* the open/closed boundary of a square edge exactly on the triangle is either way */
          int border = 0;
          for (int k = 0; k < 3; ++k) border |= x[k] % 256 == 0 || y[k] % 256 == 0;
          if (!border) { printf("FAIL touched %d meets %d at %d,%d\n", touched, meets, px, py); return 1; }
        }
      }
  }
  puts("tir reference: PASS");
  return 0;
}
'''


class DxbcTirReference(unittest.TestCase):
    def test_shared_edges_cover_each_sample_once(self):
        with tempfile.TemporaryDirectory(prefix='dxbc-tir-') as tmp:
            src = Path(tmp) / 'tir.c'
            exe = Path(tmp) / 'tir'
            src.write_text(TEST)
            built = subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                                    '-fsanitize=address,undefined', '-I', str(ROOT / 'drivers/apple-agx/mesa/dxbc'),
                                    str(src), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=120)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('tir reference: PASS', ran.stdout)


if __name__ == '__main__':
    unittest.main()
