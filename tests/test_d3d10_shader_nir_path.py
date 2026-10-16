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
  not mistaken for the code-less stream-output GS;
- D3D-H5 stream output of SM 5 programs (no TGSI record): a stream-output
  GS with SM 5 code and the vertex-shader variant of a code-less GS bound
  with an SM 5 vertex shader are created through the translator with xfb
  info (AgxD3d10CreateXfbState, D3D registers), the SM 5 vertex shader
  keeps its tokens for that variant and frees them with the shader, and
  receives the serial that keys the variant.
- EXP1314: a 4_x program using a D3D 11 opcode (an FL 11_0 device admits
  raw/structured buffers in ps_4_0: d2d1's dcl_resource_structured +
  ld_structured crashed in tgsi_to_nir) takes the translator as well, and
  EXP1315: so does one with any D3D 11 global flag (d2d1's minimum-precision
  ps_4_0, whose extended operand tokens the parser misread); a plain 4_0/4_1
  program, its immediate constant buffer and refactoring flag included,
  keeps the SM 4 parser.
Runs the real projection (see test_d3d10_shader_fields_init.py).
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

from test_d3d10_shader_fields_init import function_body, project


# The ps_4_0 of EXP1314 (d2d1.dll_00373e10): global flags with raw and
# structured buffers, dcl_resource_structured t0 (stride 16), ld_structured.
MAIN = r'''
int main() {
  const UINT structured[] = {0x00000040, 21, 0x0100486a, 0x040000a2, 0x00107000, 0, 16,
                             0x03000065, 0x001020f2, 0, 0x02000068, 1,
                             0x090000a7, 0x001020f2, 0, 0x0010000a, 0, 0x00004001, 0,
                             0x00107e46, 0};
  const UINT plain[] = {0x00000040, 12, 0x01000800 | 0x6a, 0x03000065, 0x001020f2, 0,
                        0x05000036, 0x001020f2, 0, 0x00004001, 0x3f800000, 0x0100003e};
  const UINT icb41[] = {0x00000041, 9, 0x00001835, 4, 0x3f800000, 0x40000000,
                        0x0100003e, 0x0100003e, 0x0100003e};
  const UINT minprec[] = {0x00000040, 8, 0x0101086a, 0x04000065, 0x801020f2, 0x00008001, 0,
                          0x0100003e};
  const UINT doubles[] = {0x00000041, 4, 0x0100106a, 0x0100003e};
  const UINT sm5[] = {0x00000050, 3, 0x0100003e};
  const UINT truncated[] = {0x00000040, 4, 0x0100003e, 0x05000036};
  assert(AgxD3d10TranslatorOnly(structured));
  assert(!AgxD3d10TranslatorOnly(plain));
  assert(!AgxD3d10TranslatorOnly(icb41));
  assert(AgxD3d10TranslatorOnly(minprec));
  assert(AgxD3d10TranslatorOnly(doubles));
  assert(AgxD3d10TranslatorOnly(sm5));
  assert(AgxD3d10TranslatorOnly(truncated));
  assert(!AgxD3d10TranslatorOnly(nullptr));
  return 0;
}
'''


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
            model = function_body(shader, "AgxD3d10TranslatorOnly")
            self.assertIn("((code[0] >> 4) & 0xfu) >= 5u", model)
            helper = function_body(shader, "AgxD3d10CreateShaderState")
            self.assertIn("(translator || AgxD3d10WindowsDiagnosticSwitch(", helper)
            self.assertLess(helper.index("if (state.type != PIPE_SHADER_IR_NIR && !state.tokens)"),
                            helper.index("switch (shader->type)"))
            for name in ("CreateVertexShader", "CreateGeometryShader", "CreatePixelShader"):
                body = function_body(shader, name)
                with self.subTest(name=name):
                    self.assertRegex(body, r"AgxD3d10TranslatorOnly\(\w+\) \? NULL :\s*Shader_tgsi_translate\(")
                    self.assertRegex(body, r"if \(!pShader->handle\)( \{)?\n      SetError\(hDevice, E_INVALIDARG\);")
            bind = function_body(shader, "GsSetShader")
            self.assertIn("!pShader->state.tokens && !pShader->handle", bind)
            so = function_body(shader, "CreateGeometryShaderWithStreamOutput")
            self.assertIn("if (pData->pShaderCode && !AgxD3d10TranslatorOnly(pData->pShaderCode)) {\n"
                          "      pShader->state.tokens = Shader_tgsi_translate(", so)

    def test_sm4_programs_with_d3d11_opcodes_take_the_translator(self):
        with tempfile.TemporaryDirectory(prefix="d3d10-shader-4x-") as tmp:
            out = project(Path(tmp) / "out")
            if out is None:
                self.skipTest("reference Mesa tree not available")
            shader = (out / "src/gallium/frontends/d3d10umd/Shader.cpp").read_text()
            program = ("#include <assert.h>\ntypedef unsigned int UINT;\n"
                       "static bool AgxD3d10TranslatorOnly(const UINT *code)\n" +
                       function_body(shader, "AgxD3d10TranslatorOnly") + "\n" + MAIN)
            src = Path(tmp) / "route.cpp"; exe = Path(tmp) / "route"
            src.write_text(program)
            subprocess.run([os.environ.get("CXX", "clang++"), "-std=c++17", "-Wall", "-Werror",
                            "-fsanitize=address,undefined", str(src), "-o", str(exe)], check=True)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_sm5_stream_output_through_the_translator(self):
        with tempfile.TemporaryDirectory(prefix="d3d10-shader-xfb-") as tmp:
            out = project(Path(tmp) / "out")
            if out is None:
                self.skipTest("reference Mesa tree not available")
            frontend = out / "src/gallium/frontends/d3d10umd"
            shader = (frontend / "Shader.cpp").read_text()
            xfb = function_body(shader, "AgxD3d10CreateXfbState")
            self.assertIn("xfb.outputs[i].reg = o->register_index;", xfb)
            self.assertIn("dxbc_translate_tokens_xfb(code,", xfb)
            self.assertIn("state.type = PIPE_SHADER_IR_NIR;", xfb)
            so = function_body(shader, "CreateGeometryShaderWithStreamOutput")
            self.assertIn("AgxD3d10CreateXfbState(pipe, pShader, pData->pShaderCode,\n"
                          "                             &pShader->state.stream_output)", so)
            self.assertIn("if (pData->pShaderCode && !pShader->handle)", so)
            vs = function_body(shader, "CreateVertexShader")
            self.assertLess(vs.index("pShader->agx_code = NULL;"), vs.index("AgxD3d10CreateShaderState("))
            self.assertIn("memcpy(pShader->agx_code, pCode, (size_t)pCode[1] * sizeof(UINT));", vs)
            destroy = function_body(shader, "DestroyShader")
            self.assertIn("free(pShader->agx_code);", destroy)
            bind = function_body(shader, "VsSetShader")
            self.assertIn("(pShader->state.tokens || pShader->agx_code)", bind)
            draw = (frontend / "Draw.cpp").read_text()
            self.assertIn("(pDevice->bound_vs->state.tokens || pDevice->bound_vs->agx_code)", draw)
            self.assertIn("AgxD3d10CreateXfbState(pipe, vs, vs->agx_code,", draw)
            self.assertIn("UINT *agx_code;", (frontend / "State.h").read_text())


if __name__ == "__main__":
    unittest.main()
