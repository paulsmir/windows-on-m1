"""EXP1037: CPU access to lowered textures translates the client format.

A8_UNORM (and RGB32) textures are stored as lowered physical formats
(4c7991ab: A8 -> RGBA8) because the pinned Asahi rejects alpha-only textures.
Initial data is translated, but UpdateSubresourceUP copied client bytes as
RGBA8 and Map returned the RGBA8 transfer directly. EXP1036 agx_texsel probe:
A8 dynamic Map wrote one byte per texel into a 4-byte layout, so only every
fourth texel got its alpha (1024 of 4096 px); XAML/D2D A8 masks are updated
the same way.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/mesa/scripts/build-native-asahi-state.py'


def body(name):
    text = SRC.read_text()
    start = text.index("'src/gallium/frontends/d3d10umd/Resource.cpp','" + name + "','''")
    return text[start:text.index("''')", start)]


class LoweredTextureCpuAccess(unittest.TestCase):
    def test_update_translates_lowered_formats(self):
        update = body('ResourceUpdateSubResourceUP')
        self.assertIn('AgxD3d10ClientFormat(resource)', update)
        self.assertIn('util_format_translate(dst->format,', update)

    def test_map_returns_a_client_format_view(self):
        mapped = body('ResourceMap')
        self.assertIn('AgxD3d10ClientMapBegin(', mapped)
        unmap = body('ResourceUnmap')
        self.assertIn('AgxD3d10ClientMapEnd(', unmap)
        self.assertLess(unmap.index('AgxD3d10ClientMapEnd('),
                        unmap.index('pipe_texture_unmap(device->pipe,resource->transfers[SubResource]);'))


if __name__ == '__main__':
    unittest.main()
