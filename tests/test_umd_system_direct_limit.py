"""EXP1111: unshared Mesa buffers up to the KMD class-allocation limit are direct.

EXP1110: Notepad downloaded its 2.4 MiB mapped buffer through ~1500 64-KiB
copy escapes per second (each after a synchronous completion), and DWM
uploaded 1.9-16 MiB mapped surfaces the same way, because only unshared
buffers up to 1 MiB were one CPU-visible allocation mapped by both the CPU
and the GPU. Invariants:
- the direct threshold covers a full 2560x1600 BGRA surface;
- it never exceeds the largest class allocation the KMD admits
  (render_win32_transport.c), or AllocateCb would refuse those buffers.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SystemDirectLimit(unittest.TestCase):
    def test_threshold_within_kmd_class_limit(self):
        umd = (ROOT / 'drivers/apple-agx/render-admission/umd/src/umd_internal.h').read_text()
        kmd = (ROOT / 'drivers/apple-agx/render-admission/src/render_win32_transport.c').read_text()
        threshold = int(re.search(r'#define ADMISSION_UMD_SYSTEM_DIRECT_BYTES (0x[0-9a-fA-F]+)ULL', umd).group(1), 16)
        limit = int(re.search(r'allocation->Size > (0x[0-9a-fA-F]+)ULL', kmd).group(1), 16)
        self.assertGreaterEqual(threshold, 2560 * 1600 * 4)
        self.assertLessEqual(threshold, limit)


if __name__ == '__main__':
    unittest.main()
