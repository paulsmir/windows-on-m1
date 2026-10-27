"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The per-frame GPU timeline must follow the KMD's one-job-at-a-time contract:
a job queued behind another starts no earlier than the previous completion
(no double-counted GPU time), stale render firmware starts are never used,
firmware ends are mapped with the lowest-latency offset so detection latency
is separated from GPU time, receipts drained in a later frame still belong to
the frame of their completion QPC, and another process's records are ignored.
"""
from pathlib import Path
import importlib.util
import unittest

ROOT = Path(__file__).resolve().parents[1]
HZ = 10000          # ticks per second: 10 ticks per millisecond
OFFSET = 10000      # firmware minus QPC, in ticks (1000 ms)


def load():
    path = ROOT / 'tools/agx_gpu_frame_breakdown.py'
    spec = importlib.util.spec_from_file_location('breakdown', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def row(tag, words, pid=7, qpc=0):
    return (f'{tag} hr=0x00000000 pid={pid} tid=1 session=1 qpc={qpc} ' +
            ' '.join(f'{w:08x}' for w in words))


def t(ms):
    return int(round(ms * 10))


def job(seq, kind, sub, cmp, start=0, end=None, ta_end=0, status=0, pid=7):
    """Completion + stage rows; times in ms (QPC), end/ta_end in QPC ms."""
    rows = [row('measure-gpu-completion',
                [seq, 0, 0x1000, 0, seq, kind, status, t(sub), 0, t(cmp), 0, HZ, 0], pid)]
    if end is not None:
        fw = lambda ms: t(ms) + OFFSET if ms else 0
        rows.append(row('measure-gpu-stages',
                        [seq, 0, fw(start), 0, fw(end), 0, 0, 0, fw(ta_end), 0], pid))
    return rows


def present(ms):
    return row('measure-d3d12-present', [0, 1], qpc=t(ms))


class GpuFrameBreakdown(unittest.TestCase):
    def test_serialized_spans_roles_and_late_receipts(self):
        b = load()
        log = [present(0)]
        log += job(1, 1, 10, 22, start=3, end=20, ta_end=15)       # rc_big, stale start
        log += job(2, 2, 15, 40, start=23, end=40)                  # queued behind job 1
        log += job(3, 2, 48, 51, start=50, end=50)                  # empty compute
        log += job(4, 1, 60, 60.8, start=1, end=60.5, ta_end=60.3)  # small render
        log += [present(100)]
        log += job(5, 1, 61, 92, start=2, end=91, ta_end=71)        # main, drained late
        log += job(7, 1, 110, 130, start=1, end=129, ta_end=120)    # seq 6 is foreign
        log += job(8, 2, 140, 145, start=170, end=145)              # stale compute start
        log += job(9, 2, 150, 160, start=152, end=158, pid=3)       # other process
        log += [present(200)]
        frames, summary = b.analyze(log, skip=0)
        self.assertEqual(summary['pid'], 7)
        self.assertEqual(summary['jobs'], 7)
        self.assertEqual(len(frames), 2)
        f = frames[0]
        self.assertEqual(f['jobs'], 5)
        self.assertAlmostEqual(f['gpu_span_ms'], 10 + 18 + 2 + 0.5 + 30)
        self.assertAlmostEqual(f['detect_ms'], 2 + 0 + 1 + 0.3 + 1)
        self.assertAlmostEqual(f['idle_ms'], 100 - 64.8)
        self.assertAlmostEqual(f['main_span_ms'], 30)
        self.assertAlmostEqual(f['main_3d_ms'], 20)
        self.assertEqual(f['rc_big_jobs'], 1)
        self.assertAlmostEqual(f['rc_big_span_ms'], 10)
        self.assertAlmostEqual(f['rc_big_3d_ms'], 5)
        self.assertEqual(f['r_small_jobs'], 1)
        self.assertAlmostEqual(f['r_small_span_ms'], 0.5)
        self.assertEqual(f['c_work_jobs'], 1)
        self.assertAlmostEqual(f['c_work_exact_ms'], 17)
        self.assertAlmostEqual(f['c_work_span_ms'], 18)
        self.assertEqual(f['c_empty_jobs'], 1)
        g = frames[1]
        self.assertEqual(g['jobs'], 2)
        self.assertAlmostEqual(g['gpu_span_ms'], 19 + 5)
        self.assertAlmostEqual(g['main_3d_ms'], 9)
        self.assertEqual(g['c_empty_jobs'], 1)
        self.assertEqual(g['c_work_jobs'], 0)
        self.assertAlmostEqual(g['idle_ms'], 100 - 20 - 5)

    def test_failed_job_has_no_firmware_time(self):
        b = load()
        log = [present(0)] + job(1, 1, 10, 20, status=1) + job(2, 2, 20, 30, start=21, end=30)
        log += [present(100)]
        frames, summary = b.analyze(log, skip=0)
        self.assertEqual(summary['failed_jobs'], 1)
        self.assertAlmostEqual(frames[0]['detect_ms'], 0)
        self.assertAlmostEqual(frames[0]['gpu_span_ms'], 10 + 10)


if __name__ == '__main__':
    unittest.main()
