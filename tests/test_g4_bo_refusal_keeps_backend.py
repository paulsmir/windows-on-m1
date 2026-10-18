"""EXP1265-C: a refused native BO creation must not end the device's work.

A 32 MiB render target above the class bound was refused (reject-batch kind 5,
AgxWin32NativeDeviceRange), and agx_win32_asahi_bo.c then marked the whole
native backend Failed, so the next Flush failed with E_FAIL. The device was
lost for a request the runtime reports as E_OUTOFMEMORY. The backend may only
stop when the refused creation left a Windows resource behind (rollback
failed, Backing.Live), the case the original comment guards.
"""
from pathlib import Path
import re
import unittest

SRC = Path(__file__).resolve().parents[1] / 'drivers/apple-agx/mesa/winsys/agx_win32_asahi_bo.c'


class BoRefusalKeepsBackend(unittest.TestCase):
    def test_clean_refusal_does_not_fail_the_backend(self):
        text = SRC.read_text()
        start = text.index('AgxWin32NativeDeviceCreateBo(&b->Buffers,cls,bytes,align,access,&bo->Backing);')
        branch = text[start:text.index('return NULL;', start)]
        # The fail macro runs only inside the Backing.Live (retained resource) arm.
        live = re.search(r'if\s*\(\s*bo->Backing\.Live\s*\)\s*\{[^}]*\}', branch)
        self.assertIsNotNone(live, 'no Backing.Live arm')
        self.assertIn('AGX_WIN32_ASAHI_FAIL(b, 1u)', live.group())
        outside = branch[:live.start()] + branch[live.end():]
        self.assertNotIn('AGX_WIN32_ASAHI_FAIL', outside,
                         'backend failure on a clean refused creation')


if __name__ == '__main__':
    unittest.main()
