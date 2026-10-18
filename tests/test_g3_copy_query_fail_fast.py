"""EXP1005: a copy QUERY over an invalid PTE fails without waiting.

Wom1G3PteWait receipts EXP995-EXP1003: 70 waits, 0 recovered, each 18-33 s
(1000 iterations of a 1 ms KeDelayExecutionThread at ~16 ms resolution),
stalling the client thread and DWM for up to ~330 s per run.
Invariant: predicate 57 is decided under the lock without a delay/retry.
"""
from pathlib import Path
import unittest

SRC = Path(__file__).resolve().parents[1] / 'drivers/apple-agx/render-admission/src/gpuva_g3_windows.c'


class CopyQueryFailFast(unittest.TestCase):
    def test_invalid_pte_is_not_waited_for(self):
        text = SRC.read_text()
        escape = text[text.index('NTSTATUS AdmissionGpuvaG3CopyEscape('):]
        loop = escape[escape.index('for(page=first&~0xfffULL;page<end;page+=0x1000ULL)'):
                      escape.index('COPY_REJECT_IF(!(pte->Flags&APPLE_AGX_GPUVA_G3_VALID), 57u')]
        self.assertNotIn('KeDelayExecutionThread', loop)
        self.assertNotIn('goto RetryPagingQuiescence', loop)


if __name__ == '__main__':
    unittest.main()
