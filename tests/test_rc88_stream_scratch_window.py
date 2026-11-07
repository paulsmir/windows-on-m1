"""rc88 (EXP2019b GravityMark FHD 200k): stream outputs survive back-to-back retires.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
rc87 capped the retained stream outputs per kind at the larger of 128 MiB
and the retiring batch's own outputs. GravityMark records its seven stream
draws (MaxCount 240000 at 200k: USC outputs of 30-44 MiB) in more than one
batch per frame, and the batches of a frame retire back to back. The first
retire fills the spares; the second batch's own cap (one 30 MiB draw, so
128 MiB) is then below what is already kept, so its output is released and
the next frame creates it again: EXP2019b had 1595 "GPU stream scratch
create" in 1602 presents, 467 steady-state creations of 30 MiB of at least
10 ms, 11118 winsys BO cache evictions and 1884 steady-state residency trims
(44.7 GiB). The projected stream path must create and release nothing after
the first in-flight window when a frame's draws are split six + one over two
batches and both retire before the next frame records, while never holding
more than the in-flight window's own outputs.
"""
import unittest
from test_rc71_stream_scratch import run_stream_harness


class StreamScratchWindow(unittest.TestCase):
    def test_200k_split_frame_outputs_are_reused(self):
        run_stream_harness(self, 240000, defines=('FRAME_PATTERN',))

    def test_100k_split_frame_outputs_are_reused(self):
        run_stream_harness(self, 120000, defines=('FRAME_PATTERN',))


if __name__ == '__main__':
    unittest.main()
