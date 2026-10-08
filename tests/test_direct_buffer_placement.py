"""EXP1027: CPU-visible Mesa class allocations are local-segment direct buffers.

EXP1026: system-memory (aperture) backing for GPU-accessed Mesa buffers hung
the GPU (0x116 after TDR): only the reserved local segment is UAT
representable for every 16 KiB page; system pages are 4 KiB and not reliably
16 KiB-contiguous (EXP853/854 unpublished system groups).
Invariants:
- CPU-visible Mesa class (classId != 0) allocations may only live in the
  local segment;
- CPU-visible class0 GDI/CDD surfaces stay in the aperture (EXP837);
- no allocation requests Cached (direct buffers are GPU-accessed and mostly
  CPU write-only).
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/render-admission/src/allocation_windows.c'


class DirectBufferPlacement(unittest.TestCase):
    def test_placement(self):
        text = SRC.read_text()
        mesa = text[text.index('if (classId != 0u && description->CpuVisible != 0u) {'):]
        mesa = mesa[:mesa.index('}')]
        self.assertIn('SupportedReadSegmentSet = ADMISSION_LOCAL_SEGMENT_SET', mesa)
        self.assertIn('SupportedWriteSegmentSet = ADMISSION_LOCAL_SEGMENT_SET', mesa)
        gdi = text[text.index('if (classId == 0u && description->CpuVisible != 0u) {'):]
        gdi = gdi[:gdi.index('}')]
        self.assertIn('SupportedReadSegmentSet = ADMISSION_APERTURE_SEGMENT_SET', gdi)
        self.assertLess(text.index('if (classId == 0u && description->CpuVisible'),
                        text.index('if (classId != 0u && description->CpuVisible'))
        self.assertNotIn('FlagsWddm2.Cached =', text)


if __name__ == '__main__':
    unittest.main()
