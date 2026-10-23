"""C69 (EXP1905): D3D11 reports typed UAV loads per format.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? The D3D11 runtime enables
TypedUAVLoadAdditionalFormats only when every format of Microsoft's required
set answers D3DWDDM2_0DDI_FORMAT_SUPPORT_UAV_READS; one missing format turns
the cap off and CreateComputeShader of any shader with a typed UAV load
fails with E_INVALIDARG (EXP1905 GravityMark D3D11, OCCLUDER_SCALE_SHADER).
The D3D11 set must also stay the one the D3D12 path reports (C49, validated
on J313), since both run the same formatless DXBC loads.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
STATE = ROOT / "drivers/apple-agx/mesa/scripts/build-native-asahi-state.py"
D3D12 = ROOT / "drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp"

# Microsoft, "Typed unordered access view loads": the formats that come with
# TypedUAVLoadAdditionalFormats.
REQUIRED = """R32G32B32A32_FLOAT R32G32B32A32_UINT R32G32B32A32_SINT R16G16B16A16_FLOAT
R16G16B16A16_UINT R16G16B16A16_SINT R8G8B8A8_UNORM R8G8B8A8_UINT R8G8B8A8_SINT R16_FLOAT
R16_UINT R16_SINT R8_UNORM R8_UINT R8_SINT R32_FLOAT R32_UINT R32_SINT""".split()


class D3D11TypedUavCaps(unittest.TestCase):
    def test_required_set_and_d3d12_parity(self):
        text = STATE.read_text()
        block = text[text.index("static const DXGI_FORMAT typed_uav[] = {"):]
        block = block[:block.index("};")]
        d3d11 = set(re.findall(r"DXGI_FORMAT_(\w+)", block))
        for f in REQUIRED:
            self.assertIn(f, d3d11, f)
        d3d12 = D3D12.read_text()
        d3d12 = d3d12[d3d12.index("static bool TypedUavLoadFormat(DXGI_FORMAT format) {"):]
        d3d12 = set(re.findall(r"case DXGI_FORMAT_(\w+):", d3d12[:d3d12.index("\n}\n")]))
        self.assertEqual(d3d11, d3d12)
        self.assertIn("D3D11_1DDI_FORMAT_SUPPORT_UAV_WRITES | D3DWDDM2_0DDI_FORMAT_SUPPORT_UAV_READS", text)


if __name__ == "__main__":
    unittest.main()
