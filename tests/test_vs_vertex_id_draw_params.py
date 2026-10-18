"""EXP1067: the SV_VertexID lowering must make the draw parameters available.

EXP1066 agx_vid_selftest: SV_VertexID read as k + (0, 1, 2) with a large k
(k mod 4 == 2), so full-screen triangles built from SV_VertexID collapsed
and drew nothing (atlas probe vid/vidvb rows; XAML/DComp/D2D full-screen
passes). The EXP843 lowering rewrites load_vertex_id_zero_base to
load_vertex_id - load_first_vertex after agx_shader_initialize gathered the
shader info, so SYSTEM_VALUE_FIRST_VERTEX is missing from
system_values_read; Asahi then sees uses_base_param false, never uploads the
draw parameters, skips the PARAMS push range (table 0), and the shader reads
an unwritten uniform. Invariant: after the lowering makes progress, the
shader info is gathered again before preprocessing/compilation.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/mesa/scripts/build-native-asahi-state.py'


class VertexIdDrawParams(unittest.TestCase):
    def test_lowering_regathers_info(self):
        text = SRC.read_text()
        start = text.index("state_text=state_text.replace(preprocess,'''")
        block = text[start:text.index("''',1)", start)]
        lower = block.index('agx_win32_lower_vertex_id_zero_base')
        gather = block.find('nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir))', lower)
        self.assertGreater(gather, lower, 'shader info is not regathered after the lowering')
        self.assertLess(gather, block.index('agx_preprocess_nir(nir);'))
        self.assertIn('NIR_PASS(lowered_vertex_id, nir', block)


if __name__ == '__main__':
    unittest.main()
