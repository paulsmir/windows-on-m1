"""S2-1e step 0: the D3D11 WDDM 2.0 video function table (agx_d3d11_video_ddi.inc) is generated from the pinned
WDK d3d10umddi.h. Invariant: the committed file equals the generator's output for that header, and every member
of D3DWDDM2_0DDI_VIDEODEVICEFUNCS is filled (a missing member is a NULL the runtime would call)."""
import re, subprocess, sys, tempfile, unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
INC = ROOT / 'drivers/apple-agx/mesa/d3d11/agx_d3d11_video_ddi.inc'
GEN = ROOT / 'tools/gen_d3d11_video_ddi_stubs.py'
HDR = ROOT / '.local/reference/wdk26100/d3d10umddi.h'

class VideoDdiStubs(unittest.TestCase):
    def test_generated_file_is_current_and_complete(self):
        if not HDR.exists():
            self.skipTest('pinned WDK header not available')
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / 'v.inc'
            subprocess.run([sys.executable, str(GEN), str(HDR), str(out)], check=True, capture_output=True)
            self.assertEqual(out.read_text(), INC.read_text())
        h = HDR.read_text(encoding='latin-1')
        body = re.search(r'typedef struct D3DWDDM2_0DDI_VIDEODEVICEFUNCS\s*\{(.*?)\}', h, re.S).group(1)
        members = re.findall(r'PFN\w+\s+(pfn\w+)\s*;', body)
        filled = re.findall(r'f->(pfn\w+) = ', INC.read_text())
        self.assertEqual(sorted(members), sorted(filled))
        self.assertIn('#define AGX_VIDEO_DDI_ENTRIES %du' % len(members), INC.read_text())

if __name__ == '__main__':
    unittest.main()
