"""EXP843: DWM aborted in agx_compile_shader_nir ("only for SW VS") because a
D3D SV_VertexID lowered to load_vertex_id_zero_base inside a hardware vertex
shader. The native state overlay must lower it before sysval lowering."""

from pathlib import Path
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "drivers/apple-agx/mesa/scripts/build-native-asahi-state.py"


class VertexIdZeroBaseLowering(unittest.TestCase):
    def test_hardware_vs_lowers_zero_base_vertex_id(self):
        source = SCRIPT.read_text()
        self.assertIn("nir_intrinsic_load_vertex_id_zero_base", source)
        self.assertIn("nir_isub(b, nir_load_vertex_id(b), nir_load_first_vertex(b))", source)
        self.assertIn("'\\nstatic void\\nagx_shader_initialize('", source)
        pass_use = source.index("agx_win32_lower_vertex_id_zero_base,\n")
        self.assertLess(pass_use, source.index("   agx_preprocess_nir(nir);\n'''", pass_use))
        self.assertIn("if (nir->info.stage == MESA_SHADER_VERTEX)", source[pass_use - 200:pass_use])


if __name__ == "__main__":
    unittest.main()
