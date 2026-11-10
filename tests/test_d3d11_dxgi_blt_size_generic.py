"""FS-F1: the D3D10/11 DXGI Blt box comes from the resources, not the panel.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
BltDXGI/Blt1DXGI accepted only the destination rectangle 0,0,2560,1600 and
the winsys always blitted a 2560x1600 box.  A full-screen swap chain at any
other size (1920x1080 on the scaled panel) was rejected with E_INVALIDARG,
and ResourceCopy between two presentation resources of another size blitted
a box larger than both resources.
Invariant: presentation_blt_box, the one place that turns a DXGI rectangle
into a blit box, accepts exactly the non-empty rectangles inside the real
surface, passes an accepted rectangle through unchanged (no clamping, no
panel constant), maps NULL to the whole surface, and writes nothing when it
rejects.  The helper is compiled from the shipped winsys source.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
WINSYS = ROOT / "drivers/apple-agx/mesa/winsys/agx_d3d10_windows.cpp"
HEADER = ROOT / "drivers/apple-agx/mesa/winsys/agx_d3d10_windows.h"

HARNESS = """
static void accept(UINT width, UINT height, const AGX_D3D10_WINDOWS_BLT_RECT *rect,
                   UINT x, UINT y, UINT boxWidth, UINT boxHeight) {
  UINT gx = 7u, gy = 7u, gw = 7u, gh = 7u;
  assert(presentation_blt_box(width, height, rect, &gx, &gy, &gw, &gh));
  assert(gx == x && gy == y && gw == boxWidth && gh == boxHeight);
}
static void reject(UINT width, UINT height, UINT left, UINT top, UINT right,
                   UINT bottom) {
  AGX_D3D10_WINDOWS_BLT_RECT rect = {left, top, right, bottom};
  UINT gx = 7u, gy = 7u, gw = 7u, gh = 7u;
  assert(!presentation_blt_box(width, height, &rect, &gx, &gy, &gw, &gh));
  assert(gx == 7u && gy == 7u && gw == 7u && gh == 7u);
}
int main(void) {
  UINT sink = 0u;
  AGX_D3D10_WINDOWS_BLT_RECT panel = {0u, 0u, 2560u, 1600u};
  AGX_D3D10_WINDOWS_BLT_RECT full = {0u, 0u, 1920u, 1080u};
  AGX_D3D10_WINDOWS_BLT_RECT dirty = {16u, 32u, 656u, 392u};
  AGX_D3D10_WINDOWS_BLT_RECT corner = {1919u, 1079u, 1920u, 1080u};
  /* NULL is the whole subresource of whatever size the resource has. */
  accept(1920u, 1080u, NULL, 0u, 0u, 1920u, 1080u);
  accept(2560u, 1600u, NULL, 0u, 0u, 2560u, 1600u);
  accept(1u, 1u, NULL, 0u, 0u, 1u, 1u);
  /* Inside the bounds: passed through unchanged. */
  accept(2560u, 1600u, &panel, 0u, 0u, 2560u, 1600u);
  accept(1920u, 1080u, &full, 0u, 0u, 1920u, 1080u);
  accept(1920u, 1080u, &dirty, 16u, 32u, 640u, 360u);
  accept(2560u, 1600u, &dirty, 16u, 32u, 640u, 360u);
  accept(1920u, 1080u, &corner, 1919u, 1079u, 1u, 1u);
  /* The panel rectangle does not fit a smaller resource. */
  reject(1920u, 1080u, 0u, 0u, 2560u, 1600u);
  /* Out of bounds on one axis only. */
  reject(1920u, 1080u, 0u, 0u, 1921u, 1080u);
  reject(1920u, 1080u, 0u, 0u, 1920u, 1081u);
  reject(1920u, 1080u, 1920u, 0u, 1921u, 1080u);
  reject(1920u, 1080u, 0u, 0u, 0xffffffffu, 1080u);
  /* Empty. */
  reject(1920u, 1080u, 0u, 0u, 0u, 0u);
  reject(1920u, 1080u, 100u, 0u, 100u, 1080u);
  reject(1920u, 1080u, 0u, 100u, 1920u, 100u);
  /* Inverted, including a pair whose unsigned difference would wrap. */
  reject(1920u, 1080u, 200u, 0u, 100u, 1080u);
  reject(1920u, 1080u, 0u, 200u, 1920u, 100u);
  reject(1920u, 1080u, 0xfffffff0u, 0u, 16u, 1080u);
  /* No surface, a surface pipe_box cannot address, a missing output. */
  reject(0u, 1080u, 0u, 0u, 1u, 1u);
  reject(1920u, 0u, 0u, 0u, 1u, 1u);
  reject(0x80000000u, 1080u, 0u, 0u, 1u, 1u);
  reject(1920u, 0x80000000u, 0u, 0u, 1u, 1u);
  assert(!presentation_blt_box(1920u, 1080u, &full, NULL, &sink, &sink, &sink));
  assert(!presentation_blt_box(1920u, 1080u, &full, &sink, &sink, &sink, NULL));
  return 0;
}
"""


class DxgiBltSizeGeneric(unittest.TestCase):
    def test_blt_rectangle_is_validated_against_the_real_surface(self):
        rect = re.search(r"typedef struct _AGX_D3D10_WINDOWS_BLT_RECT \{.*?\} "
                         r"AGX_D3D10_WINDOWS_BLT_RECT;", HEADER.read_text(), re.S)
        helper = re.search(r"static BOOL presentation_blt_box\(.*?^}",
                           WINSYS.read_text(), re.S | re.M)
        self.assertIsNotNone(rect)
        self.assertIsNotNone(helper)
        program = ("#include <assert.h>\n#include <stddef.h>\n"
                   "typedef int BOOL;\ntypedef unsigned int UINT;\n"
                   "#define TRUE 1\n#define FALSE 0\n"
                   + rect.group(0) + "\n" + helper.group(0) + "\n" + HARNESS)
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "blt_box.c"
            executable = Path(temp) / "blt_box"
            path.write_text(program)
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined", str(path),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            subprocess.run([str(executable)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
