"""C71 (EXP1906): D3D11 render targets with GENERATE_MIPS and unordered access.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? GravityMark D3D11 creates
"RGf32 Target Surface Mipmaps" (render target + shader resource + unordered
access, GENERATE_MIPS); the frontend admitted GENERATE_MIPS only with exactly
RT|SRV and refused it (CreateResource E_NOTIMPL -> device removed at 41 s).
Microsoft requires only that RT and SRV are among the binds.
"""
from pathlib import Path
import unittest

STATE = Path(__file__).resolve().parents[1] / "drivers/apple-agx/mesa/scripts/build-native-asahi-state.py"


class D3D11RenderTargetContract(unittest.TestCase):
    def test_generate_mips_admits_unordered_access(self):
        text = STATE.read_text()
        rt = text[text.index("bool autogen = pCreateResource->MiscFlags == D3D10_DDI_RESOURCE_AUTO_GEN_MIP_MAP;"):]
        rt = rt[:rt.index('LOG_UNSUPPORTED("A render target outside the D3D10 contract");')]
        self.assertIn("(autogen && (pCreateResource->BindFlags & ~D3D11_DDI_BIND_UNORDERED_ACCESS) ==\n"
                      "              (D3D10_DDI_BIND_RENDER_TARGET | D3D10_DDI_BIND_SHADER_RESOURCE))", rt)
        self.assertNotIn("autogen && pCreateResource->BindFlags ==", rt)
        self.assertIn('"reject-d3d11-render-target"', text)


if __name__ == "__main__":
    unittest.main()
