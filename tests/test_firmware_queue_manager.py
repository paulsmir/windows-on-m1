"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
A live rc124 slot must be distinguishable from a rebind (including a restore)
in the existing timing receipt, without changing the default receipt or
misclassifying an unmanaged job. Counts must survive binary decoding.
"""
import struct
import unittest
from test_render_job_timing_rc83 import build_and_run, INCLUDE, SOURCE, load


class ManagerReceipt(unittest.TestCase):
    def test_classification(self):
        build_and_run(self, r'''
#include <assert.h>
#include "render_job_timing.h"
int main(void) {
  assert(AdmissionJobTimingManagerFlags(0,1,1) == 0u);
  assert(AdmissionJobTimingManagerFlags(1,0,1) == 0u);
  assert(AdmissionJobTimingManagerFlags(1,1,1) == 0x100000u);
  assert(AdmissionJobTimingManagerFlags(1,1,0) == 0x200000u);
  return 0;
}
''', [SOURCE], INCLUDE)

    def test_decoded_counts(self):
        b = load()
        data = bytearray(struct.pack('<IIIIQQ', 4, 32 + 64 * 288, 4, 0, 1000000, 0))
        for i in range(64):
            fields = [0] * 41
            if i < 4:
                fields[0] = i + 1
                fields[1] = 7
                fields[6:18] = [1000 + i * 100 + j for j in range(12)]
                fields[26] = [0x100000, 0x200008, 0x200088, 0][i]
            data += b.KMD_SLOT_V4.pack(*fields)
        counts = b.kmd_job_timing(bytes(data))['build_flags']
        self.assertEqual(counts['manager_live'], 1)
        self.assertEqual(counts['manager_rebind'], 2)
        self.assertEqual(counts['manager_restored'], 1)


if __name__ == '__main__':
    unittest.main()
