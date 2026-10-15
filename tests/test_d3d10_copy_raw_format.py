"""D3D-G10b: ResourceCopy / ResourceCopyRegion keep the stored bits.

The bring-up copy chose its blit format from three byte families or the
source format itself: SNORM copies blitted as SNORM, which does not
round-trip (-128 becomes -127; Asahi agx_blit.c copies snorm as sint), other
same-family pairs (R16G16B16A16_FLOAT to _UINT, R32_FLOAT to _UINT, ...)
were refused, and compressed copies were refused.
Invariant: the bring-up byte families keep their UNORM blit; a same-format
copy blits in the linear format, snorm as sint; a cross-format copy inside
one DXGI cast family blits as the unsigned format of the block size; depth
formats copy only to themselves; compressed formats take the
resource_copy_region path (no blit format).
"""
import ast
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "drivers/apple-agx/mesa/scripts/build-native-asahi-state.py"


def helper():
    text = SCRIPT.read_text()
    start = text.index("static enum pipe_format\nAgxD3d10CopyRawFormat(")
    end = text.index("\n}\n", start) + 3
    fam_start = text.index("static unsigned\nAgxD3d10CopyFamily(DXGI_FORMAT format)")
    fam_end = text.index("\n}\n", fam_start) + 3
    return text[fam_start:fam_end] + "\n" + text[start:end]


PROGRAM = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
typedef enum { DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_B8G8R8A8_TYPELESS, DXGI_FORMAT_B8G8R8A8_UNORM,
  DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8X8_TYPELESS, DXGI_FORMAT_B8G8R8X8_UNORM,
  DXGI_FORMAT_B8G8R8X8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM,
  DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R8G8B8A8_SNORM,
  DXGI_FORMAT_R8G8B8A8_SINT, DXGI_FORMAT_R16G16B16A16_TYPELESS, DXGI_FORMAT_R16G16B16A16_FLOAT,
  DXGI_FORMAT_R16G16B16A16_UINT, DXGI_FORMAT_R16G16B16A16_SNORM, DXGI_FORMAT_R32_TYPELESS,
  DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_BC1_UNORM } DXGI_FORMAT;
enum pipe_format { PIPE_FORMAT_NONE, PIPE_FORMAT_R8_UINT, PIPE_FORMAT_R16_UINT, PIPE_FORMAT_R32_UINT,
  PIPE_FORMAT_R32G32_UINT, PIPE_FORMAT_R32G32B32A32_UINT, PIPE_FORMAT_R8G8B8A8_UNORM,
  PIPE_FORMAT_B8G8R8A8_UNORM, PIPE_FORMAT_R16G16B16A16_FLOAT, PIPE_FORMAT_R16G16B16A16_UINT,
  PIPE_FORMAT_R16G16B16A16_SNORM, PIPE_FORMAT_R16G16B16A16_SINT, PIPE_FORMAT_R32_FLOAT,
  PIPE_FORMAT_Z32_FLOAT, PIPE_FORMAT_R16G16_FLOAT, PIPE_FORMAT_DXT1_RGBA, PIPE_FORMAT_R8G8B8A8_SRGB };
typedef struct { DXGI_FORMAT Format; } Resource;
struct pipe_resource { enum pipe_format format; };
typedef unsigned UINT;
static bool util_format_is_compressed(enum pipe_format f) { return f == PIPE_FORMAT_DXT1_RGBA; }
static bool util_format_is_snorm(enum pipe_format f) { return f == PIPE_FORMAT_R16G16B16A16_SNORM; }
static enum pipe_format util_format_snorm_to_sint(enum pipe_format f) { (void)f; return PIPE_FORMAT_R16G16B16A16_SINT; }
static enum pipe_format util_format_linear(enum pipe_format f) { return f == PIPE_FORMAT_R8G8B8A8_SRGB ? PIPE_FORMAT_R8G8B8A8_UNORM : f; }
static bool util_format_is_depth_or_stencil(enum pipe_format f) { return f == PIPE_FORMAT_Z32_FLOAT; }
static unsigned util_format_get_blocksize(enum pipe_format f) {
  return f == PIPE_FORMAT_R16G16B16A16_FLOAT || f == PIPE_FORMAT_R16G16B16A16_UINT ||
         f == PIPE_FORMAT_R16G16B16A16_SNORM ? 8 : 4; }
static DXGI_FORMAT AgxD3d10WindowsCastParent(DXGI_FORMAT f, UINT *ro) {
  (void)ro;
  switch (f) {
  case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R16G16B16A16_FLOAT:
  case DXGI_FORMAT_R16G16B16A16_UINT: case DXGI_FORMAT_R16G16B16A16_SNORM:
    return DXGI_FORMAT_R16G16B16A16_TYPELESS;
  case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_R32_FLOAT: case DXGI_FORMAT_D32_FLOAT:
    return DXGI_FORMAT_R32_TYPELESS;
  default: return DXGI_FORMAT_UNKNOWN;
  }
}
@@HELPER@@
static enum pipe_format raw(DXGI_FORMAT sd, enum pipe_format sp, DXGI_FORMAT dd, enum pipe_format dp) {
  Resource s = {sd}, d = {dd}; struct pipe_resource a = {sp}, b = {dp};
  return AgxD3d10CopyRawFormat(&s, &d, &a, &b);
}
int main(void) {
  assert(raw(DXGI_FORMAT_B8G8R8A8_UNORM, PIPE_FORMAT_B8G8R8A8_UNORM,
             DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, PIPE_FORMAT_B8G8R8A8_UNORM) == PIPE_FORMAT_B8G8R8A8_UNORM);
  assert(raw(DXGI_FORMAT_R8G8B8A8_SNORM, PIPE_FORMAT_R8G8B8A8_UNORM,
             DXGI_FORMAT_R8G8B8A8_UINT, PIPE_FORMAT_R8G8B8A8_UNORM) == PIPE_FORMAT_R8G8B8A8_UNORM);
  assert(raw(DXGI_FORMAT_R16G16B16A16_SNORM, PIPE_FORMAT_R16G16B16A16_SNORM,
             DXGI_FORMAT_R16G16B16A16_SNORM, PIPE_FORMAT_R16G16B16A16_SNORM) == PIPE_FORMAT_R16G16B16A16_SINT);
  assert(raw(DXGI_FORMAT_R16G16B16A16_FLOAT, PIPE_FORMAT_R16G16B16A16_FLOAT,
             DXGI_FORMAT_R16G16B16A16_UINT, PIPE_FORMAT_R16G16B16A16_UINT) == PIPE_FORMAT_R32G32_UINT);
  assert(raw(DXGI_FORMAT_R32_FLOAT, PIPE_FORMAT_R32_FLOAT,
             DXGI_FORMAT_D32_FLOAT, PIPE_FORMAT_Z32_FLOAT) == PIPE_FORMAT_NONE);
  assert(raw(DXGI_FORMAT_R32_FLOAT, PIPE_FORMAT_R32_FLOAT,
             DXGI_FORMAT_R16G16_FLOAT, PIPE_FORMAT_R16G16_FLOAT) == PIPE_FORMAT_NONE);
  assert(raw(DXGI_FORMAT_BC1_UNORM, PIPE_FORMAT_DXT1_RGBA,
             DXGI_FORMAT_BC1_UNORM, PIPE_FORMAT_DXT1_RGBA) == PIPE_FORMAT_NONE);
  return 0;
}
'''


class CopyRawFormat(unittest.TestCase):
    def test_copies_keep_the_stored_bits(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "copy.c"; exe = Path(tmp) / "copy"
            src.write_text(PROGRAM.replace("@@HELPER@@", helper()))
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Werror",
                            "-Wno-unused-function", "-fsanitize=address,undefined", str(src), "-o", str(exe)],
                           check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
