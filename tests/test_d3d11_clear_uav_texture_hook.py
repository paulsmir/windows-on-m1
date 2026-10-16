"""EXP1330: typed texture UAV clears do not call a missing clear_texture.

Invariant caught: Asahi implements no pipe->clear_texture (nor
clear_render_target), and a NULL hook is a call to address 0 -- the fault
class EXP1330 found in ClearView. ClearUnorderedAccessView of a typed
texture view called pipe->clear_texture unconditionally. It uses the hook
when the driver has one and Gallium's u_default_clear_texture otherwise,
over the view's level and slices; buffer views keep clear_buffer. Runs the
real clear_typed against stubs.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "drivers/apple-agx/mesa/d3d11/agx_d3d11_ddi.cpp"

PROGRAM = r'''
#include <cassert>
#include <cstdint>
enum pipe_texture_target { PIPE_BUFFER, PIPE_TEXTURE_2D, PIPE_TEXTURE_3D };
enum pipe_format { FMT };
struct pipe_box { int x, y, z, width, height, depth; };
struct pipe_resource { pipe_texture_target target; unsigned width0, height0, array_size; };
struct pipe_context;
typedef void (*clear_texture_fn)(pipe_context *, pipe_resource *, unsigned, const pipe_box *, const void *);
struct pipe_context {
  void (*clear_buffer)(pipe_context *, pipe_resource *, unsigned, unsigned, const void *, int);
  clear_texture_fn clear_texture; };
struct Device { pipe_context *pipe; };
struct ImageView { pipe_resource *resource; pipe_format format;
  struct { struct { unsigned offset, size; } buf; struct { unsigned level, first_layer, last_layer; } tex; } u; };
struct AgxUnorderedAccessView { ImageView image; };
static unsigned u_minify(unsigned v, unsigned l) { v >>= l; return v ? v : 1; }
static void u_box_3d(int x, int y, int z, int w, int h, int d, pipe_box *b) { *b = {x, y, z, w, h, d}; }
static int util_format_get_blocksize(pipe_format) { return 4; }
static unsigned hooks, defaults, buffers; static pipe_box last;
static void hook(pipe_context *, pipe_resource *, unsigned, const pipe_box *b, const void *) { ++hooks; last = *b; }
static void u_default_clear_texture(pipe_context *, pipe_resource *, unsigned, const pipe_box *b, const void *) {
  ++defaults; last = *b; }
static void clear_buffer(pipe_context *, pipe_resource *, unsigned, unsigned, const void *, int) { ++buffers; }
@@FUNCTION@@
int main() {
  pipe_context asahi = {clear_buffer, nullptr}, other = {clear_buffer, hook};
  Device d = {&asahi};
  pipe_resource tex = {PIPE_TEXTURE_2D, 64, 32, 6}, buf = {PIPE_BUFFER, 256, 1, 1};
  AgxUnorderedAccessView v = {{&tex, FMT, {{0, 0}, {1, 2, 4}}}};
  const uint8_t pixel[16] = {};
  clear_typed(&d, &v, pixel);
  assert(defaults == 1 && hooks == 0);
  assert(last.width == 32 && last.height == 16 && last.z == 2 && last.depth == 3);
  d.pipe = &other; clear_typed(&d, &v, pixel);
  assert(hooks == 1 && defaults == 1);
  AgxUnorderedAccessView b = {{&buf, FMT, {{16, 64}, {0, 0, 0}}}};
  d.pipe = &asahi; clear_typed(&d, &b, pixel);
  assert(buffers == 1 && defaults == 1);
  return 0;
}
'''


class D3d11ClearUavTextureHook(unittest.TestCase):
    def test_missing_clear_texture_uses_the_gallium_default(self):
        text = SRC.read_text()
        function = re.search(r"static void\nclear_typed\(.*?\n\}", text, re.S).group(0)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "c.cpp"; exe = Path(tmp) / "c"
            src.write_text(PROGRAM.replace("@@FUNCTION@@", function))
            subprocess.run([os.environ.get("CXX", "clang++"), "-std=c++17", "-Wall", "-Werror",
                            "-Wno-unused-function", "-fsanitize=address,undefined", str(src), "-o", str(exe)],
                           check=True)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main()
