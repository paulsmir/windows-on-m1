"""rc103 receipt-only: slow private scene requests (umd_gpuva_windows.c
private_escape, measure-scene-escape).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP2037a: four scene ACQUIRE escapes took 1.29 s of GravityMark's scored
frame 1 -> 2 with no receipt of their own, so the startup breakdown charged
that time to the next receipt (measure-touch, "residency"). Every scene
request of at least 10 ms must close its own interval with its operation,
time, geometry and result; the fast cached requests (about 50 us each, ~2500
a frame during uploads) must stay silent, and the wait-profile site around
the escape must be unchanged.
"""
import re
import unittest

from test_dxbc_to_nir import ROOT
from test_d3d12_view_mapping import block

SOURCE = ROOT / 'drivers/apple-agx/render-admission/umd/src/umd_gpuva_windows.c'


class SceneEscapeReceipt(unittest.TestCase):
    def test_slow_scene_requests_have_their_own_receipt(self):
        body = block(SOURCE.read_text(), 'static int private_escape(void *context, APPLE_AGX_G3_PRIVATE_REQUEST *payload) {')
        start = body.index('(void)QueryPerformanceCounter(&scene_start);')
        begin = body.index('AdmissionUmdWaitBegin(AgxWaitSceneEscape);')
        escape = body.index('device->KernelCallbacks->pfnEscapeCb(')
        end = body.index('AdmissionUmdWaitEnd(wait_pfnEscapeCb_5);')
        stop = body.index('(void)QueryPerformanceCounter(&scene_end);')
        self.assertTrue(start < begin < escape < end < stop)
        gate = body.index('if(qpc_at_least_10ms(scene_end.QuadPart-scene_start.QuadPart)) {')
        emit = body.index('AdmissionUmdDiagnostic("measure-scene-escape",hr,values,ARRAYSIZE(values));')
        self.assertTrue(stop < gate < emit)
        values = re.search(r'UINT values\[7\]=\{([^}]*)\};', body[gate:emit]).group(1).split(',')
        self.assertEqual([v.strip() for v in values],
                         ['operation', 'qpc_us(scene_end.QuadPart-scene_start.QuadPart)', 'width', 'height',
                          'utile', 'layers|(samples<<8)', '(UINT)hr'])
        # the refusal receipt is unchanged
        self.assertIn('AdmissionUmdDiagnostic("reject-private-escape",hr,values,ARRAYSIZE(values));', body)
        self.assertEqual(body.count('AdmissionUmdDiagnostic('), 2)


if __name__ == '__main__':
    unittest.main()
