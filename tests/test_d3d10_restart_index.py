"""EXP1031: IaSetIndexBuffer sets the primitive-restart index from the index size.

EXP1030 hardware probe (agx_inst_selftest): non-indexed and instanced draws
correct, but DrawIndexed drew 240 of 1024 pixels and a BaseVertexLocation draw
none; Direct2D/XAML text and icons (indexed quads) were missing. The patched
IaSetIndexBuffer set restart_index = 0 while DrawIndexed enables primitive
restart, so every primitive referencing vertex 0 was cut. Upstream Mesa
d3d10umd uses 0xffff for R16_UINT and 0xffffffff for R32_UINT.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/mesa/scripts/build-native-asahi-state.py'


class RestartIndex(unittest.TestCase):
    def test_restart_index_matches_index_size(self):
        text = SRC.read_text()
        body = text[text.index("'IaSetIndexBuffer','''"):]
        body = body[:body.index("''')")]
        self.assertIn('pDevice->restart_index = indexSize == 2u ? 0xffffu : 0xffffffffu;', body)
        bound = body[body.index('pDevice->index_size = indexSize;'):]
        self.assertNotIn('pDevice->restart_index = 0;', bound)


if __name__ == '__main__':
    unittest.main()
