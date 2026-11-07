"""rc87 (EXP2017 GravityMark FHD 200k): large stream outputs are retained, not re-created.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
At 200k GravityMark's GPU stream draws need USC outputs of about 25-37 MiB
each (MaxCount 200000), more than 128 MiB per batch. The fixed 128 MiB
per-kind spare cap (agx_win32_stream_scratch.h) then released part of every
retired batch's outputs and the next batch created them again: EXP2017b had
1005 "GPU stream scratch create" (14 at 100k), 396 creations of 30 MiB
(30 s), 848 steady-state residency trims of 30 MiB and 7722 winsys BO cache
evictions. The projected stream path (same harness as RC71: seven draws per
batch, four batches in flight) must create no BO and release none once the
first in-flight window is past, with draws of 200000 records.
"""
import unittest
from test_rc71_stream_scratch import run_stream_harness


class LargeStreamScratch(unittest.TestCase):
    def test_200k_stream_outputs_are_reused_after_their_batch_retires(self):
        run_stream_harness(self, 200000)


if __name__ == '__main__':
    unittest.main()
