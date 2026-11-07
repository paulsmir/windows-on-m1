"""C74: IaSetIndexBuffer accepts every bind an index buffer can be created with.

EXP1908 (GravityMark D3D11): reject-seterror fn=IaSetIndexBuffer line=228
E_NOTIMPL, then device removed at 64 s. Buffer creation accepts
INDEX | VERTEX | SHADER_RESOURCE | UNORDERED_ACCESS | STREAM_OUTPUT, and
since EXP1297 a buffer UAV is PIPE_BIND_SHADER_BUFFER (SHADER_IMAGE is
removed from buffers), while IaSetIndexBuffer still allowed only
INDEX | SHADER_IMAGE. Invariant: each pipe bind those D3D binds produce is
in IaSetIndexBuffer's accepted mask.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "drivers/apple-agx/mesa/scripts/build-native-asahi-state.py"


class IndexBufferBinds(unittest.TestCase):
    def test_index_buffer_mask_covers_creatable_buffer_binds(self):
        text = SCRIPT.read_text()
        body = text[text.index("'IaSetIndexBuffer','''"):]
        body = body[:body.index("SetError(hDevice, E_NOTIMPL)")]
        mask = re.search(r"~\(PIPE_BIND_INDEX_BUFFER([^)]*)\)", body).group(0)
        for bind in ("PIPE_BIND_VERTEX_BUFFER", "PIPE_BIND_SAMPLER_VIEW",
                     "PIPE_BIND_SHADER_BUFFER", "PIPE_BIND_STREAM_OUTPUT"):
            self.assertIn(bind, mask)
        # the D3D binds buffer creation accepts for an index buffer
        create = text[text.index("const UINT bufferBindMask"):]
        create = create[:create.index(";")]
        for d3d in ("D3D10_DDI_BIND_VERTEX_BUFFER", "D3D10_DDI_BIND_SHADER_RESOURCE",
                    "D3D11_DDI_BIND_UNORDERED_ACCESS", "D3D10_DDI_BIND_STREAM_OUTPUT"):
            self.assertIn(d3d, create)


if __name__ == "__main__":
    unittest.main()
