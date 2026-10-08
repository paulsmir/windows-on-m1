"""EXP1033: CPU writes made through a dynamic buffer's shadow reach its BO at Unmap.

EXP1032 DDI trace (agx_glyph_selftest): Direct2D writes glyph bits into a fresh
dynamic vertex buffer with Map(WRITE_NOOVERWRITE), unmaps it, then copies the
range into its DEFAULT R8_UINT atlas buffer with ResourceCopyRegion. The first
map of a never-mapped BO goes to resource->dynamic_shadow and was only marked
shadow_dirty; only IaSetVertexBuffers published the shadow, so the copy (and an
index-buffer bind, or a vertex buffer bound before its first map) read zeros.
The first glyphs of every Direct2D atlas were lost ('Hello World' 0 px, later
glyphs drawn).
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/mesa/scripts/build-native-asahi-state.py'


def body(name):
    text = SRC.read_text()
    start = text.index("'src/gallium/frontends/d3d10umd/Resource.cpp','" + name + "','''")
    return text[start:text.index("''')", start)]


class DynamicShadowPublish(unittest.TestCase):
    def test_unmap_publishes_shadow_writes_to_the_bo(self):
        unmap = body('ResourceUnmap')
        branch = unmap[unmap.index('if (resource->shadow_only_map) {'):]
        branch = branch[:branch.index('} else if (resource->direct_buffer_map)')]
        self.assertIn('AgxWin32AsahiBufferWriteMap(device->pipe,resource->resource)', branch)
        self.assertIn('memcpy(map,resource->dynamic_shadow,resource->logical_bytes);', branch)
        self.assertNotIn('resource->shadow_dirty=true;', branch)


if __name__ == '__main__':
    unittest.main()
