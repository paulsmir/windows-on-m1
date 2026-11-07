"""EXP1064: DrawIndexed(IndexCount 0) is a D3D no-op, not an error.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The projected d3d10umd DrawIndexed reported E_NOTIMPL through SetErrorCb for
IndexCount 0 (the same contract as EXP1062's zero-vertex native draws, one
layer up). The other draw entries already return silently for zero counts.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/mesa/scripts/build-native-asahi-state.py'


def body(text, name):
    start = text.index("replace_function_body('src/gallium/frontends/d3d10umd/Draw.cpp','%s','''" % name)
    part = text[start:]
    return part[:part.index("''')")]


class EmptyIndexedDraw(unittest.TestCase):
    def test_zero_index_count_returns_before_any_error(self):
        text = SRC.read_text()
        draw = body(text, 'DrawIndexed')
        guard = draw.index('if (pDevice && !IndexCount) return;')
        self.assertLess(guard, draw.index('SetError('))
        self.assertNotIn('!IndexCount ||', draw)
        # Instanced zero-count ordering is executed, including invalid unused
        # state, by test_d3d11_zero_draw rather than matched as source text.



if __name__ == '__main__':
    unittest.main()
