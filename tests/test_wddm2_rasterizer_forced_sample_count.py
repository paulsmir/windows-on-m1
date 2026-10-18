"""EXP1324: WDDM 2.0 rasterizer states with ForcedSampleCount 1 are accepted.

Invariant caught: with feature level 11_0 reported by default, the desktop's
D3D11.1 processes create rasterizer states with ForcedSampleCount = 1;
rasterizer10 refused every non-zero count with E_NOTIMPL and the processes
recreated their devices without end (10249 reject-wddm2-rasterizer and
7721 adapter opens in one boot). D3D11_RASTERIZER_DESC1: 0 means not forced;
1 rasterizes single-sampled (no depth-stencil view, no depth test) -- what
a single-sampled or attachment-less render (D3D-G11) already does. Counts
above 1 need target-independent rasterization, which is not reported, and
conservative rasterization stays refused.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "drivers/apple-agx/mesa/d3d11/agx_wddm2_ddi.cpp"

PROGRAM = r'''
#include <cassert>
typedef int BOOL; typedef unsigned UINT; typedef int INT; typedef float FLOAT; typedef long HRESULT;
#define E_NOTIMPL ((HRESULT)0x80004001L)
struct D3D10DDI_HDEVICE { void *p; };
enum { D3DWDDM2_0DDI_CONSERVATIVE_RASTERIZATION_OFF = 0, D3DWDDM2_0DDI_CONSERVATIVE_RASTERIZATION_ON = 1 };
struct D3DWDDM2_0DDI_RASTERIZER_DESC { int FillMode, CullMode; BOOL FrontCounterClockwise; INT DepthBias;
  FLOAT DepthBiasClamp, SlopeScaledDepthBias; BOOL DepthClipEnable, ScissorEnable, MultisampleEnable,
  AntialiasedLineEnable; UINT ForcedSampleCount; int ConservativeRasterizationMode; };
struct D3D10_DDI_RASTERIZER_DESC { int FillMode, CullMode; BOOL FrontCounterClockwise; INT DepthBias;
  FLOAT DepthBiasClamp, SlopeScaledDepthBias; BOOL DepthClipEnable, ScissorEnable, MultisampleEnable,
  AntialiasedLineEnable; };
static unsigned refusals;
static void refuse(D3D10DDI_HDEVICE, const char *, HRESULT, UINT) { ++refusals; }
@@FUNCTION@@
int main() {
  D3D10DDI_HDEVICE dev = {0};
  D3DWDDM2_0DDI_RASTERIZER_DESC in = {3, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, D3DWDDM2_0DDI_CONSERVATIVE_RASTERIZATION_OFF};
  D3D10_DDI_RASTERIZER_DESC out = {};
  assert(rasterizer10(dev, &in, &out) && out.FillMode == 3 && out.DepthClipEnable == 1);
  in.ForcedSampleCount = 1;  /* the desktop's FL 11_0 state */
  assert(rasterizer10(dev, &in, &out) && refusals == 0);
  in.ForcedSampleCount = 4;
  assert(!rasterizer10(dev, &in, &out) && refusals == 1);
  in.ForcedSampleCount = 0; in.ConservativeRasterizationMode = D3DWDDM2_0DDI_CONSERVATIVE_RASTERIZATION_ON;
  assert(!rasterizer10(dev, &in, &out) && refusals == 2);
  assert(!rasterizer10(dev, nullptr, &out));
  return 0;
}
'''


class Wddm2RasterizerForcedSampleCount(unittest.TestCase):
    def test_forced_sample_count_one_is_single_sampled(self):
        text = SRC.read_text()
        function = re.search(r"bool\nrasterizer10\(.*?\n\}", text, re.S).group(0)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "r.cpp"; exe = Path(tmp) / "r"
            src.write_text(PROGRAM.replace("@@FUNCTION@@", function))
            subprocess.run([os.environ.get("CXX", "clang++"), "-std=c++17", "-Wall", "-Werror",
                            "-fsanitize=address,undefined", str(src), "-o", str(exe)], check=True)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main()
