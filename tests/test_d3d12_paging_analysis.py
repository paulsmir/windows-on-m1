"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Top-four wait receipts censor smaller buckets. Never report absent paging as
zero, add nested inclusive waits, assign delayed completions by print time,
or call all CPU waits GPU idle. Exercise the streaming analyzer on exact QPCs.
"""
from pathlib import Path
import importlib.util
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]


def load():
    path = ROOT / 'tools/agx_paging_wait_analyze.py'
    assert path.exists(), 'paging wait attribution analyzer missing'
    sys.path.insert(0, str(path.parent))
    try:
        spec = importlib.util.spec_from_file_location('paging_analysis', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module
    finally:
        sys.path.pop(0)


def row(tag, words, qpc=0, pid=7, tid=1):
    return (f'{tag} hr=0x00000000 pid={pid} tid={tid} qpc={qpc} ' +
            ' '.join(f'{w:08x}' for w in words))


class PagingAnalysis(unittest.TestCase):
    def test_censored_buckets_and_interval_intersection(self):
        m = load()
        # All units here are microseconds (1 MHz). 10 ms frame; two jobs
        # leave a 3 ms gap [4000,7000]. Execute spans [2000,8000], of which
        # 3 ms is gap and 3 ms is concurrent with outstanding jobs.
        lines = [row('measure-d3d12-present', [], 0),
                 row('measure-d3d12-execute', [1, 10, 5000, 1000, 1], 8001),
                 # Another thread must not steal the replay-window join.
                 row('measure-d3d12-execute', [1, 0, 0, 0, 99], 8001, pid=8, tid=2),
                 row('measure-d3d12-replay-window', [2000, 0, 7000, 0, 8000, 0, 1000000, 0], 8002),
                 row('measure-d3d12-wait', [1, 1, 200, 4,
                     0x403, 1, 1800, 0x402, 2, 600, 0x603, 1, 400, 0x30f, 1, 300], 8003),
                 row('measure-d3d12-present', [], 10000),
                 # Completions arrive late in the log, but QPCs place them.
                 row('measure-gpu-completion', [1, 0, 0, 0, 1, 2, 0, 1000, 0, 4000, 0, 1000000, 0], 11000),
                 row('measure-gpu-completion', [2, 0, 0, 0, 2, 2, 0, 7000, 0, 9000, 0, 1000000, 0], 11001)]
        out = m.analyze(iter(lines), skip=0)
        self.assertEqual(out['frames'], 1)
        self.assertEqual(out['cpu_ms']['replay']['mean'], 5)
        self.assertEqual(out['cpu_ms']['submit']['mean'], 1)
        self.assertEqual(out['buckets_ms']['0x402']['mean'], .6)
        self.assertEqual(out['buckets_ms']['0x41c']['mean'], 0)
        self.assertEqual(out['target_bounds_ms']['paging']['mean_lower'], .6)
        self.assertAlmostEqual(out['target_bounds_ms']['paging']['mean_upper'], .8)
        self.assertEqual(out['target_bounds_ms']['pool_map']['mean_upper'], .2)
        self.assertEqual(out['gaps_ms']['own_complete_to_next_submit']['mean'], 3)
        self.assertEqual(out['gaps_ms']['inside_execute']['mean'], 3)
        # 3.1 ms known in-window waits cannot fit in 3 ms non-gap: >=0.1 ms.
        self.assertAlmostEqual(out['gaps_ms']['all_wait_overlap_lower']['mean'], .1)
        self.assertEqual(out['gaps_ms']['all_wait_overlap_upper']['mean'], 3)
        self.assertEqual(out['gaps_ms']['paging_overlap_upper']['mean'], .8)
        # Profiling starts before the replay window. Phase-other work must
        # not force a positive lower bound on wait time inside that window.
        lines[4] = row('measure-d3d12-wait', [1, 1, 200, 4,
            0x2, 1, 1800, 0x402, 2, 600, 0x603, 1, 400, 0x30f, 1, 300], 8003)
        self.assertEqual(m.analyze(iter(lines), 0)['gaps_ms']['all_wait_overlap_lower']['mean'], 0)

    def test_top_four_bound_invalid_and_untruncated(self):
        m = load()
        words = [1, 1, 1000, 4, 0x403, 1, 900, 0x303, 1, 800,
                 0x603, 1, 700, 0x30f, 1, 100]
        # Missing one exact key <= smallest retained bucket, not all omitted.
        self.assertEqual(m.wait_bound(words, {0x402}), (0, 100))
        self.assertEqual(m.wait_bound(words, {0x402, 0x602}), (0, 200))
        words[2] = words[3] = 0
        self.assertEqual(m.wait_bound(words, {0x402}), (0, 0))
        words[3] = 1
        with self.assertRaisesRegex(ValueError, 'invalid'):
            m.wait_bound(words, {0x402})


if __name__ == '__main__':
    unittest.main()
