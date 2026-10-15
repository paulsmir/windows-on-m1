"""The DXBC -> NIR shader path of the projected D3D10 frontend.

Invariants caught:
- every ordinary Create{Vertex,Geometry,Pixel}Shader builds its pipe state
  through AgxD3d10CreateShaderState, so the translator is used for each of
  them by default (EXP1295; APPLE_AGX_D3D_SHADER_TGSI=1 opts a process out)
  and TGSI stays the fallback for programs the translator refuses;
- a shader with stream output never takes the NIR path: Asahi's state
  creation rebuilds nir_xfb_info only from TGSI output declarations and
  refuses a NIR state carrying pipe stream output (state creation would
  return NULL and the runtime would see a failed shader);
- CreateGeometryShaderWithStreamOutput keeps creating its TGSI state
  directly.
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
            self.assertIn('!AgxD3d10WindowsDiagnosticSwitch("APPLE_AGX_D3D_SHADER_TGSI")', helper)
            self.assertIn("state.type = PIPE_SHADER_IR_NIR;", helper)
            for name in ("CreateVertexShader", "CreateGeometryShader", "CreatePixelShader"):
                body = function_body(shader, name)
                self.assertIn("AgxD3d10CreateShaderState(pipe, pShader,", body, name)
                self.assertIsNone(re.search(r"create_(vs|gs|fs)_state\(pipe, &pShader->state\)", body), name)
            so = function_body(shader, "CreateGeometryShaderWithStreamOutput")
            self.assertNotIn("AgxD3d10CreateShaderState", so)
            self.assertIn("create_gs_state(pipe, &pShader->state)", so)


if __name__ == "__main__":
    unittest.main()
