"""C72: ResourceCopyRegion of a block-compressed mip smaller than one block.

EXP1907 (GravityMark D3D11): CopySubresourceRegion of BC7 128x128 level 6
(2x2 texels) with the block-aligned box 4x4 was refused with E_NOTIMPL
(reject-resource-copy-region 98 98 2 7 2 2 2 2 6 70 4 4) because the box was
checked against the 2x2 level size; the runtime removed the device. D3D
validates compressed copy boxes against the block-aligned level size.
Invariant: the extent a copy box may address is the level size rounded up to
the format's block size (unchanged for uncompressed formats), and the copy
itself is clamped to the level's texels.
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
    start = text.index("static unsigned\nAgxD3d10CopyExtent(")
    return text[start:text.index("\n}\n", start) + 3]


PROGRAM = r'''
#include <assert.h>
HELPER
int main(void) {
  assert(AgxD3d10CopyExtent(2u, 4u) == 4u);   /* EXP1907: BC7 level 6 */
  assert(AgxD3d10CopyExtent(1u, 4u) == 4u);
  assert(AgxD3d10CopyExtent(6u, 4u) == 8u);
  assert(AgxD3d10CopyExtent(8u, 4u) == 8u);
  assert(AgxD3d10CopyExtent(2u, 1u) == 2u);   /* uncompressed */
  assert(AgxD3d10CopyExtent(33u, 1u) == 33u);
  return 0;
}
'''


class CopyBlockExtent(unittest.TestCase):
    def test_compressed_boxes_address_whole_blocks(self):
        with tempfile.TemporaryDirectory(prefix="d3d10-copy-extent-") as tmp:
            source = Path(tmp) / "extent.c"
            source.write_text(PROGRAM.replace("HELPER", helper()))
            binary = Path(tmp) / "extent"
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Werror",
                            str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_validation_uses_the_aligned_extent_and_copy_clamps_to_texels(self):
        text = SCRIPT.read_text()
        body = text[text.index("/* D3D10 ResourceCopyRegion for textures"):]
        body = body[:body.index("reject-resource-copy-region")]
        self.assertIn("right <= srcWa && bottom <= srcHa", body)
        self.assertIn("right - left <= dstWa - DstX", body)
        self.assertIn("!(left % bw) && !(top % bh)", body)
        copy = text[text.index("/* The level's texels inside the block-aligned box (C72). */"):]
        self.assertIn("MIN2(right, srcW) - left", copy[:400])


if __name__ == "__main__":
    unittest.main()
