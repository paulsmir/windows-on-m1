"""A submission may write more allocations than the WDK's written primaries.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? EXP1851 ModelViewer: with
the per-dispatch barrier gone, one compute batch holds a frame's dispatches
and writes more than 16 BOs; the winsys refused it (AGX_G4_WRITTEN_LIMIT 16,
copied from D3DDDI_MAX_WRITTEN_PRIMARIES), the backend failed and Asahi's next
agx_bo_create returned NULL inside agx_fast_link (c0000005). The written list
marks every GPU-written slot; only direct displayable slots are
WrittenPrimaries, which the WDK bounds by 16. The winsys and UMD limits must
agree, exceed 16, and the primaries bound must stay on WrittenPrimaries. A
D3D12-only process must report refusals (reject-batch).
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
WINSYS = ROOT / 'drivers/apple-agx/mesa/winsys/agx_win32_gpuva_batch.c'
UMD = ROOT / 'drivers/apple-agx/render-admission/umd/src/umd_gpuva_windows.c'
OWNER = ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp'


class GpuvaWrittenLimit(unittest.TestCase):
    def test_written_limits_agree_and_exceed_the_primaries_bound(self):
        winsys = int(re.search(r'#define AGX_G4_WRITTEN_LIMIT (\d+)u', WINSYS.read_text()).group(1))
        umd_text = UMD.read_text()
        umd = int(re.search(r'#define ADMISSION_UMD_WRITTEN_LIMIT (\d+)u', umd_text).group(1))
        self.assertEqual(winsys, umd)
        self.assertGreater(winsys, 16)
        submit = umd_text[umd_text.index('static int submit(void *context, const uint64_t *written,'):]
        submit = submit[:submit.index('\n}\n')]
        self.assertIn('written_count > ADMISSION_UMD_WRITTEN_LIMIT ||', submit)
        self.assertNotIn('written_count > D3DDDI_MAX_WRITTEN_PRIMARIES', submit)
        self.assertIn('if (request.NumPrimaries==D3DDDI_MAX_WRITTEN_PRIMARIES) {', submit)

    def test_the_d3d12_owner_reports_batch_refusals(self):
        owner = OWNER.read_text()
        create = owner[owner.index('owner->Context = AgxWin32AsahiContextCreate(owner->Screen, &owner->Owner);'):]
        create = create[:create.index('\n  }\n')]
        self.assertIn('AgxWin32BatchRefusalHook = owner_batch_refusal;', create)
        self.assertIn('AgxWin32BackendFailHook = owner_backend_fail;', create)
        self.assertIn('AdmissionUmdDiagnostic("reject-batch", E_FAIL, values, 4u);', owner)


if __name__ == '__main__':
    unittest.main()
