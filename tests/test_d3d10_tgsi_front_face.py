"""EXP1298: the projected PS front-face input follows Asahi's TGSI contract."""
from pathlib import Path
import tempfile
import unittest

from test_d3d10_shader_fields_init import function_body, project


class FrontFaceTGSI(unittest.TestCase):
    def test_ps_sgv_face_is_integer_system_value(self):
        with tempfile.TemporaryDirectory(prefix="d3d10-face-") as tmp:
            out = project(Path(tmp) / "out")
            if out is None:
                self.skipTest("reference Mesa tree not available")
            shader = (out / "src/gallium/frontends/d3d10umd/ShaderTGSI.c").read_text()
            body = function_body(shader, "dcl_ps_sgv_input")
            face = body[body.index("if (dcl_siv_name == D3D10_SB_NAME_IS_FRONT_FACE) {"):]
            self.assertIn("ureg_DECL_system_value(ureg, TGSI_SEMANTIC_FACE, 0)", body)
            self.assertIn("ureg_USNE(ureg, tmp, ureg_scalar(reg, TGSI_SWIZZLE_X)", face)
            self.assertIn("ureg_imm1u(ureg, 0)", face)
            self.assertNotIn("ureg_CMP(", face)


if __name__ == "__main__":
    unittest.main()
