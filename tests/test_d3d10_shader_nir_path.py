"""The DXBC -> NIR shader path of the projected D3D10 frontend.

Invariants caught:
- every ordinary Create{Vertex,Geometry,Pixel}Shader builds its pipe state
  through AgxD3d10CreateShaderState, so the translator is used (behind
  APPLE_AGX_D3D_SHADER_NIR) for each of them and TGSI stays the fallback;
- a shader with stream output never takes the NIR path: Asahi's state
  creation rebuilds nir_xfb_info only from TGSI output declarations and
  refuses a NIR state carrying pipe stream output (state creation would
  return NULL and the runtime would see a failed shader);
- CreateGeometryShaderWithStreamOutput keeps creating its TGSI state
  directly;
- D3D-H5: an SM 5 program (FL 11_0) never reaches Mesa's DXBC parser,
  which knows SM 4 opcodes only and indexes its opcode table with an
  assert (an out-of-bounds read in the release build): VS/GS/PS of model 5
  always take the translator, a refused one fails its creation instead of
  creating a state without tokens, an SM 5 GS (no tokens, a NIR state) is
  not mistaken for the code-less stream-output GS, and a stream-output GS
  with SM 5 code is refused.
Runs the real projection (see test_d3d10_shader_fields_init.py).
"""
from pathlib import Path
import re
import tempfile
import unittest

from test_d3d10_shader_fields_init import function_body, project


class ShaderNirPath(unittest.TestCase):
    def test_ordinary_shaders_use_the_nir_capable_helper(self):
        with tempfile.TemporaryDirectory(prefix="d3d10-shader-nir-") as tmp:
            out = project(Path(tmp) / "out")
            if out is None:
                self.skipTest("reference Mesa tree not available")
            shader = (out / "src/gallium/frontends/d3d10umd/Shader.cpp").read_text()
            helper = function_body(shader, "AgxD3d10CreateShaderState")
            self.assertRegex(helper, r"!state\.stream_output\.num_outputs\s*&&")
            self.assertIn('AgxD3d10WindowsDiagnosticSwitch("APPLE_AGX_D3D_SHADER_NIR")', helper)
            self.assertIn("state.type = PIPE_SHADER_IR_NIR;", helper)
            for name in ("CreateVertexShader", "CreateGeometryShader", "CreatePixelShader"):
                body = function_body(shader, name)
                self.assertIn("AgxD3d10CreateShaderState(pipe, pShader,", body, name)
                self.assertIsNone(re.search(r"create_(vs|gs|fs)_state\(pipe, &pShader->state\)", body), name)
            so = function_body(shader, "CreateGeometryShaderWithStreamOutput")
            self.assertNotIn("AgxD3d10CreateShaderState", so)
            self.assertIn("create_gs_state(pipe, &pShader->state)", so)

    def test_sm5_programs_never_reach_the_sm4_parser(self):
        with tempfile.TemporaryDirectory(prefix="d3d10-shader-sm5-") as tmp:
            out = project(Path(tmp) / "out")
            if out is None:
                self.skipTest("reference Mesa tree not available")
            shader = (out / "src/gallium/frontends/d3d10umd/Shader.cpp").read_text()
            model = function_body(shader, "AgxD3d10ShaderModel5")
            self.assertIn("((code[0] >> 4) & 0xfu) >= 5u", model)
            helper = function_body(shader, "AgxD3d10CreateShaderState")
            self.assertIn("(sm5 || AgxD3d10WindowsDiagnosticSwitch(", helper)
            self.assertLess(helper.index("if (state.type != PIPE_SHADER_IR_NIR && !state.tokens)"),
                            helper.index("switch (shader->type)"))
            for name in ("CreateVertexShader", "CreateGeometryShader", "CreatePixelShader"):
                body = function_body(shader, name)
                with self.subTest(name=name):
                    self.assertRegex(body, r"AgxD3d10ShaderModel5\(\w+\) \? NULL :\s*Shader_tgsi_translate\(")
                    self.assertIn("if (!pShader->handle)\n      SetError(hDevice, E_INVALIDARG);", body)
            bind = function_body(shader, "GsSetShader")
            self.assertIn("!pShader->state.tokens && !pShader->handle", bind)
            so = function_body(shader, "CreateGeometryShaderWithStreamOutput")
            self.assertLess(so.index("AgxD3d10ShaderModel5(pData->pShaderCode)"),
                            so.index("Shader_tgsi_translate("))


if __name__ == "__main__":
    unittest.main()
