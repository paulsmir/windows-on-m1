"""EXP1170: the GL present copies the shown image out of its write-combined
mapping in concurrent row slices.

Invariant: for any height and slice count the slices are contiguous and
cover every row exactly once, and the copy honours the source stride (the
staging mapping pitch can exceed width * 4) without writing past the
destination rows. A gap or overlap in the partition shows up on screen as
stale or torn horizontal bands.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ICD = ROOT / "drivers/apple-agx/windows/icd"

PROGRAM = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "agx_wgl_present_copy.h"
int main(void) {
  for (unsigned slices = 1; slices <= 9; ++slices) {
    for (unsigned height = 0; height <= 67; ++height) {
      unsigned expect = 0;
      for (unsigned i = 0; i < slices; ++i) {
        unsigned first, end;
        agx_wgl_slice_rows(height, slices, i, &first, &end);
        assert(first == expect && end >= first);
        expect = end;
      }
      assert(expect == height);
    }
  }
  /* 2560x1600 does not overflow the row arithmetic. */
  unsigned first, end;
  agx_wgl_slice_rows(1600u, AGX_WGL_COPY_SLICES, AGX_WGL_COPY_SLICES - 1u, &first, &end);
  assert(first == 1200u && end == 1600u);
  /* Strided source, packed destination with a guard byte after the image. */
  const unsigned w = 5, h = 7, src_stride = w * 4 + 12;
  unsigned char *src = malloc(src_stride * h), *dst = malloc(w * 4 * h + 1);
  for (unsigned i = 0; i < src_stride * h; ++i) src[i] = (unsigned char)(i * 7 + 1);
  memset(dst, 0xee, w * 4 * h + 1);
  for (unsigned i = 0; i < 3; ++i) {
    agx_wgl_slice_rows(h, 3, i, &first, &end);
    agx_wgl_copy_rows(dst, w * 4, src, src_stride, w * 4, first, end);
  }
  for (unsigned y = 0; y < h; ++y)
    assert(memcmp(dst + y * w * 4, src + y * src_stride, w * 4) == 0);
  assert(dst[w * 4 * h] == 0xee);
  puts("PASS");
  return 0;
}
'''


class PresentCopySlices(unittest.TestCase):
    def test_slices_cover_every_row_once(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "t.c"
            exe = Path(tmp) / "t"
            src.write_text(PROGRAM)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Werror", "-I", str(ICD),
                            str(src), "-o", str(exe)], check=True)
            out = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
            self.assertIn("PASS", out.stdout)

    def test_present_uses_the_sliced_copy(self):
        text = (ICD / "agx_wgl_icd.cpp").read_text()
        self.assertIn('#include "agx_wgl_present_copy.h"', text)
        self.assertIn("agx_wgl_slice_rows(", text)
        self.assertIn("SubmitThreadpoolWork(", text)
        self.assertIn("WaitForThreadpoolWorkCallbacks(", text)


if __name__ == "__main__":
    unittest.main()
