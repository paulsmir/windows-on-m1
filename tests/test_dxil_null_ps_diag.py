"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
APPLE_AGX_D3D12_NULL_PS deletes the output writes of one pixel shader so a
PERF arm can measure that shader's frame cost. It must stay inert unless the
variable names exactly one 16-hex FNV-1a hash of the DXIL bytes, touch only
fragment shaders, and run before the NIR is handed back.
"""
import unittest
from pathlib import Path

DXIL = Path(__file__).resolve().parents[1] / 'drivers/apple-agx/mesa/dxil/agx_dxil_to_nir.c'


class NullPsDiag(unittest.TestCase):
    def test_exact_opt_in_and_fragment_only(self):
        text = DXIL.read_text()
        body = text[text.index('agx_dxil_diag_null_fragment(nir_shader *nir'):]
        body = body[:body.index('\n}\n')]
        self.assertIn('getenv("APPLE_AGX_D3D12_NULL_PS")', body)
        self.assertIn('strlen(want) != 16', body)
        self.assertIn('nir->info.stage != MESA_SHADER_FRAGMENT', body)
        self.assertIn('0xcbf29ce484222325ull', body)
        self.assertIn('0x100000001b3ull', body)
        self.assertIn('if (strcmp(have, want))', body)

    def test_called_before_result(self):
        text = DXIL.read_text()
        call = text.index('agx_dxil_diag_null_fragment(nir, data, bytes);')
        self.assertLess(call, text.index('result->nir = nir;', call))


if __name__ == '__main__':
    unittest.main()
