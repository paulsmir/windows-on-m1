"""D3D12 C4b: the UMD device storage on a D3D12 command queue's context.

Invariants caught (investigation/analysis/D3D12-C4-copy.md):
- the D3D12 initializer never creates a kernel context (the queue owns it:
  d3d12core rejects contexts that belong to another queue) and marks the
  context borrowed;
- a borrowed context is never destroyed by the device (initializer failure
  paths, DestroyKernelContext);
- the D3D10/11 initializer still creates its own context first and then runs
  the same shared attach (paging queue, render fence, screen).
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'drivers/apple-agx/render-admission/umd/src/umd_runtime_device.c'


def body(text, name):
    """The function from its definition to the next top-level '}' line
    (preprocessor branches make brace counting unreliable)."""
    start = text.index(name + '(')
    end = text.index('\n}\n', start)
    return text[start:end + 3]


class QueueOwnedDevice(unittest.TestCase):
    def test_queue_initializer_borrows_the_context(self):
        text = SOURCE.read_text()
        queue = body(text, 'HRESULT AdmissionUmdRuntimeDeviceInitializeForQueue')
        self.assertNotIn('pfnCreateContextVirtualCb', queue)
        self.assertNotIn('pfnCreateContextCb', queue)
        self.assertIn('device->KernelContext = queueContext;', queue)
        self.assertIn('device->BorrowedContext = TRUE;', queue)
        self.assertIn('device->Win32Generation = generation;', queue)
        self.assertTrue(queue.rstrip().endswith('return AdmissionUmdRuntimeDeviceAttach(device);\n}'))

    def test_borrowed_context_is_never_destroyed(self):
        text = SOURCE.read_text()
        attach = body(text, 'static HRESULT AdmissionUmdRuntimeDeviceAttach')
        destroys = len(re.findall(r'pfnDestroyContextCb\(', attach))
        guarded = len(re.findall(r'if \(!device->BorrowedContext\)\s+\(void\)device->KernelCallbacks->pfnDestroyContextCb\(', attach))
        self.assertGreater(destroys, 0)
        self.assertEqual(destroys, guarded)
        destroy = body(text, 'HRESULT AdmissionUmdRuntimeDeviceDestroyKernelContext')
        self.assertLess(destroy.index('device->BorrowedContext'), destroy.index('pfnDestroyContextCb('))

    def test_d3d11_initializer_still_creates_then_attaches(self):
        text = SOURCE.read_text()
        d3d11 = body(text, 'HRESULT AdmissionUmdRuntimeDeviceInitialize')
        self.assertIn('pfnCreateContextVirtualCb', d3d11)
        self.assertLess(d3d11.index('pfnCreateContextVirtualCb'), d3d11.index('AdmissionUmdRuntimeDeviceAttach(device)'))
        self.assertNotIn('BorrowedContext = TRUE', d3d11)


if __name__ == '__main__':
    unittest.main()
