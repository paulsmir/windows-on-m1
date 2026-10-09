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

    def test_direct_maps_skip_the_full_shadow_copy(self):
        """EXP1090: EXP1089 CSwitch samples put 29 % of DWM's composition thread
        in memcpy under ResourceUnmap: every Unmap of a directly mapped dynamic
        buffer copied the whole buffer back into its CPU shadow. A NO_OVERWRITE
        map now maps the BO itself; the shadow is only a fallback and is marked
        stale (never handed out) once direct writes bypass it."""
        unmap = body('ResourceUnmap')
        self.assertNotIn('memcpy(resource->dynamic_shadow, resource->active_buffer_map', unmap)
        self.assertIn('resource->shadow_stale = true;', unmap)
        self.assertIn('resource->active_buffer_map != resource->dynamic_shadow', unmap)
        mapping = body('ResourceMap')
        nooverwrite = mapping[mapping.index('if (DDIMap == D3D10_DDI_MAP_WRITE_NOOVERWRITE) {'):]
        self.assertLess(nooverwrite.index('AgxWin32AsahiBufferWriteMap(device->pipe,resource->resource)'),
                        nooverwrite.index('resource->active_buffer_map=resource->dynamic_shadow;'))
        self.assertLess(nooverwrite.index('if (resource->shadow_stale)'),
                        nooverwrite.index('resource->active_buffer_map=resource->dynamic_shadow;'))


if __name__ == '__main__':
    unittest.main()
