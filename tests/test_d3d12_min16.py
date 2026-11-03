"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
M16 (GravityMark's Tellusim maps half to min16float): the 16-bit minimum
precision caps and the native 16-bit dxil-spirv lowering are opt-in until
hardware validates them. The default caps must keep reporting NONE (every
existing hardware result was taken with it), both switches take exactly "1",
and every caps structure the UMD answers reports the same value. The
default compile stays byte-identical (a captured GravityMark corpus of 243
shaders contains no min-precision type, so their bytes cannot change).
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DDI = ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp'
DXIL = ROOT / 'drivers/apple-agx/mesa/dxil/agx_dxil_to_nir.c'


class Min16(unittest.TestCase):
    def test_caps_default_none_and_exact_opt_in(self):
        text = DDI.read_text()
        body = text[text.index('static D3D12DDI_SHADER_MIN_PRECISION MinPrecisionCaps()'):]
        body = body[:body.index('return caps;')]
        self.assertIn('"APPLE_AGX_D3D12_MIN16_CAPS"', body)
        self.assertIn("== 1 && value[0] == '1'", body)
        self.assertIn(': D3D12DDI_SHADER_MIN_PRECISION_NONE', body)
        self.assertIn('? D3D12DDI_SHADER_MIN_PRECISION_16_BIT', body)

    def test_every_shader_caps_answer_uses_one_value(self):
        text = DDI.read_text()
        self.assertEqual(len(re.findall(r'd\.MinPrecision\s*=\s*MinPrecisionCaps\(\);', text)), 2)
        self.assertNotRegex(text, r'd\.MinPrecision\s*=\s*D3D12DDI_SHADER_MIN_PRECISION_(NONE|16_BIT)')

    def test_native_16bit_lowering_is_exact_opt_in(self):
        text = DXIL.read_text()
        helper = text[text.index('static bool agx_dxil_min16_native(void)'):]
        helper = helper[:helper.index('}') + 1]
        self.assertIn('getenv("APPLE_AGX_DXIL_MIN16")', helper)
        self.assertIn("env && env[0] == '1' && !env[1]", helper)
        self.assertIn('DXIL_SPV_OPTION_MIN_PRECISION_NATIVE_16BIT', text)
        self.assertIn('agx_dxil_min16_native()', text[text.index('agx_checked_run(converter)') - 900:])


if __name__ == '__main__':
    unittest.main()
