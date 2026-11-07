"""Descriptor-heap resources are tracked on an initialised batch.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? EXP1854/EXP1855 x64 probe
C47B: bind_heaps registered heap UAV writes (AgxWin32AsahiBatchWriteResources)
before launch_grid on a fresh compute batch; Asahi's agx_batch_writes_internal
asserts batch->initialized, which only agx_batch_init_state sets (the
draw/launch paths call it before tracking), so the assert-enabled x64 UMD
stopped in a message box (dump: agx_batch.c batch->initialized, first write).
The helpers must initialise the batch state before tracking reads or writes.
"""
from pathlib import Path
import unittest

SCENE = Path(__file__).resolve().parents[1] / 'drivers/apple-agx/mesa/winsys/agx_win32_asahi_scene.c'


def body(text, name):
    start = text.index(name + '(struct pipe_context *pctx')
    return text[start:text.index('\n}\n', start)]


class HeapBatchTracking(unittest.TestCase):
    def test_tracking_helpers_initialise_the_batch_first(self):
        text = SCENE.read_text()
        for name, track in (('AgxWin32AsahiBatchReadResources', 'agx_batch_reads('),
                            ('AgxWin32AsahiBatchWriteResources', 'agx_batch_writes')):
            fn = body(text, name)
            self.assertIn('agx_batch_init_state(batch);', fn, name)
            self.assertLess(fn.index('agx_batch_init_state(batch);'), fn.index(track), name)


if __name__ == '__main__':
    unittest.main()
