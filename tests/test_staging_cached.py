"""EXP1017: UMD staging allocations are CPU-cached; GPU-visible ones are not.

EXP1016: DWM spent 23.5 s hashing 2.1 GB of staging at 91 MB/s (write-
combined CPU reads) and copy escapes ran at 77 MB/s; the upload phase was 57%
of active frame time. DXGK_ALLOCATIONINFOFLAGS_WDDM2_0 says to set Cached for
allocations the UMD reads and never on the primary.
Invariant: Cached is set exactly for CPU-visible Mesa class (classId != 0)
allocations, never for CPU-invisible (GPU-local, Direct, primary) or class0
GDI/CDD allocations, and it is computed after the final CpuVisible value.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/render-admission/src/allocation_windows.c'


class StagingCached(unittest.TestCase):
    def test_cached_only_for_cpu_visible_mesa_class(self):
        text = SRC.read_text()
        sets = re.findall(r'info->FlagsWddm2\.Cached\s*=\s*([^;]+);', text)
        self.assertEqual(sets, ['classId != 0u && description->CpuVisible != 0u'])
        cached = text.index('info->FlagsWddm2.Cached')
        visible = text.index('info->FlagsWddm2.CpuVisible = description->CpuVisible != 0u;')
        self.assertLess(visible, cached)
        self.assertLess(text.index('info->FlagsWddm2.Value = 0u;'), cached)
        tail = text[cached:text.index('return STATUS_SUCCESS;', cached)]
        self.assertNotIn('FlagsWddm2.Value', tail)


if __name__ == '__main__':
    unittest.main()
