"""Catch the real D3D-G5 bug: the projected frontend disables triangle MSAA.

The D3D 10.1+/11 invariant is that MultisampleEnable controls line algorithm,
while the rasterizer multisamples triangles on a multisample target. The 10.0
DDI retains its legacy FALSE behavior.
"""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_d3d10_shader_fields_init import project, function_body


class MsaaRasterizerProjection(unittest.TestCase):
    def test_projected_rasterizer_contract(self):
        with tempfile.TemporaryDirectory(prefix="d3d10-msaa-raster-") as tmp:
            out = project(Path(tmp) / "out")
            if out is None:
                self.skipTest("reference Mesa tree not available")
            raster = function_body((out / "src/gallium/frontends/d3d10umd/Rasterizer.cpp").read_text(),
                                   "CreateRasterizerState")
            state = (out / "src/gallium/frontends/d3d10umd/State.h").read_text()
            device = function_body((out / "src/gallium/frontends/d3d10umd/Device.cpp").read_text(),
                                   "CreateDevice")
            self.assertIn("state.multisample = !pDevice->agx_d3d10_0_interface ||", raster)
            self.assertIn("pRasterizerDesc->MultisampleEnable", raster)
            self.assertIn("state.line_rectangular = pRasterizerDesc->MultisampleEnable", raster)
            self.assertIn("state.line_smooth = !pDevice->agx_d3d10_0_interface &&", raster)
            self.assertIn("!pRasterizerDesc->MultisampleEnable &&", raster)
            self.assertIn("bool agx_d3d10_0_interface;", state)
            self.assertIn("pDevice->agx_d3d10_0_interface =", device)
            self.assertIn("D3D10_0_DDI_INTERFACE_VERSION", device)


if __name__ == "__main__":
    unittest.main()
