"""D3D-G10b: ResourceCopyRegion between subresources of one D3D resource type.

EXP1292: CopySubresourceRegion from a Texture2DArray slice (pipe target
2D_ARRAY) into a single-slice Texture2D staging texture (pipe target 2D)
was refused with E_NOTIMPL because the frontend compared pipe targets; the
runtime removed the device and the format smoke's view checks stopped.
D3D copies between subresources of the same resource type, and a pipe
target only records slice count or cube-ness of a 1D/2D resource.
Invariant: 1D/1D_ARRAY, 2D/2D_ARRAY/CUBE/CUBE_ARRAY and 3D are three copy
families; buffers have none (they take the buffer path).
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "drivers/apple-agx/mesa/scripts/build-native-asahi-state.py"


def helper():
    text = SCRIPT.read_text()
    start = text.index("static unsigned\nAgxD3d10CopyTargetFamily(")
    return text[start:text.index("\n}\n", start) + 3]


PROGRAM = r'''
#include <assert.h>
enum pipe_texture_target { PIPE_BUFFER, PIPE_TEXTURE_1D, PIPE_TEXTURE_2D, PIPE_TEXTURE_3D,
  PIPE_TEXTURE_CUBE, PIPE_TEXTURE_RECT, PIPE_TEXTURE_1D_ARRAY, PIPE_TEXTURE_2D_ARRAY,
  PIPE_TEXTURE_CUBE_ARRAY };
HELPER
#define SAME(a, b) (AgxD3d10CopyTargetFamily(a) && AgxD3d10CopyTargetFamily(a) == AgxD3d10CopyTargetFamily(b))
int main(void) {
  assert(SAME(PIPE_TEXTURE_2D_ARRAY, PIPE_TEXTURE_2D));   /* EXP1292 */
  assert(SAME(PIPE_TEXTURE_CUBE, PIPE_TEXTURE_2D));
  assert(SAME(PIPE_TEXTURE_CUBE_ARRAY, PIPE_TEXTURE_2D_ARRAY));
  assert(SAME(PIPE_TEXTURE_1D_ARRAY, PIPE_TEXTURE_1D));
  assert(SAME(PIPE_TEXTURE_3D, PIPE_TEXTURE_3D));
  assert(!SAME(PIPE_TEXTURE_3D, PIPE_TEXTURE_2D));
  assert(!SAME(PIPE_TEXTURE_1D, PIPE_TEXTURE_2D));
  assert(!SAME(PIPE_BUFFER, PIPE_BUFFER));
  return 0;
}
'''


class CopyTargetFamily(unittest.TestCase):
    def test_slices_and_single_textures_copy_within_their_family(self):
        with tempfile.TemporaryDirectory(prefix="d3d10-copy-family-") as tmp:
            source = Path(tmp) / "family.c"
            source.write_text(PROGRAM.replace("HELPER", helper()))
            binary = Path(tmp) / "family"
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Werror",
                            str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
