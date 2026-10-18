"""EXP1027: CPU-visible Mesa class allocations are local-segment direct buffers.

EXP1026: system-memory (aperture) backing for GPU-accessed Mesa buffers hung
the GPU (0x116 after TDR): only the reserved local segment is UAT
representable for every 16 KiB page; system pages are 4 KiB and not reliably
16 KiB-contiguous (EXP853/854 unpublished system groups).
Invariants:
- CPU-visible Mesa class (classId != 0) allocations prefer the local
  segment within the admitted CPU-visible set (EXP1027: local-only refused);
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
        # EXP1027: dxgkrnl refuses local-only CPU-visible class allocations;
        # keep the admitted CPU-visible set with the local segment preferred.
        self.assertNotIn('if (classId != 0u && description->CpuVisible != 0u) {', text)
        self.assertIn('info->PreferredSegment.SegmentId0 = ADMISSION_MEMORY_LOCAL_SEGMENT;', text)
        self.assertIn('? ADMISSION_CPU_VISIBLE_SEGMENT_SET', text)
        gdi = text[text.index('if (classId == 0u && description->CpuVisible != 0u) {'):]
        gdi = gdi[:gdi.index('}')]
        self.assertIn('SupportedReadSegmentSet = ADMISSION_APERTURE_SEGMENT_SET', gdi)
        self.assertNotIn('FlagsWddm2.Cached =', text)


if __name__ == '__main__':
    unittest.main()
