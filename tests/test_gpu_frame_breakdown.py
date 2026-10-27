"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The per-frame GPU timeline must follow the KMD's one-job-at-a-time contract:
a job queued behind another starts no earlier than the previous completion
(no double-counted GPU time), stale render firmware starts are never used,
firmware ends are mapped with the lowest-latency offset so detection latency
is separated from GPU time, receipts drained in a later frame still belong to
the frame of their completion QPC, and another process's records are ignored.
rc76: the fixed per-job cost is measured from the same bound -- a compute
job's kick (mapped firmware start minus the bound) and a small render job's
time to its TA end -- and a stale start never yields a kick. The KMD
Wom1JobTiming971 decoder follows render_job_timing.h's v1 layout (a wrong
version or size is refused) and counts the completion-to-kick gap only for
jobs the one-job-at-a-time worker kept waiting.
rc79: a render's measure-gpu-prelude row (completion v3) splits its pre-3D
time into the compute prelude (exact firmware start/end), the kick before
it and the TA after it (TA end - prelude end); renders without the row, or
whose prelude stamps are stale, contribute nothing to the split.
"""
from pathlib import Path
import importlib.util
import struct
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


def prelude(seq, start, end, pid=7):
    fw = lambda ms: t(ms) + OFFSET
    return row('measure-gpu-prelude', [seq, 0, fw(start), 0, fw(end), 0], pid)


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
        # rc76: job 2 starts 1 ms after job 1 completed, job 3 2 ms after its submit
        self.assertEqual(f['c_kick_jobs'], 2)
        self.assertAlmostEqual(f['c_kick_ms'], 1 + 2)
        self.assertAlmostEqual(f['r_small_ta_ms'], 0.3)
        g = frames[1]
        self.assertEqual(g['jobs'], 2)
        self.assertAlmostEqual(g['gpu_span_ms'], 19 + 5)
        self.assertAlmostEqual(g['main_3d_ms'], 9)
        self.assertEqual(g['c_empty_jobs'], 1)
        self.assertEqual(g['c_work_jobs'], 0)
        self.assertAlmostEqual(g['idle_ms'], 100 - 20 - 5)
        self.assertEqual(g['c_kick_jobs'], 0)  # job 8's start is stale

    def test_failed_job_has_no_firmware_time(self):
        b = load()
        log = [present(0)] + job(1, 1, 10, 20, status=1) + job(2, 2, 20, 30, start=21, end=30)
        log += [present(100)]
        frames, summary = b.analyze(log, skip=0)
        self.assertEqual(summary['failed_jobs'], 1)
        self.assertAlmostEqual(frames[0]['detect_ms'], 0)
        self.assertAlmostEqual(frames[0]['gpu_span_ms'], 10 + 10)


def kmd_snapshot(slots, hz=1000000, version=1):
    """slots: (fence, pid, {phase: qpc})"""
    b = load()
    data = bytearray(struct.pack('<IIIIQQ', version, 32 + 64 * 168, len(slots), 0, hz, 0))
    for i in range(64):
        fence, pid, q = slots[i] if i < len(slots) else (0, 0, {})
        data += struct.pack('<IIIIQQ12Q4QII', fence, pid, 0, 0, 0, 0,
                            *[q.get(p, 0) for p in b.KMD_PHASES], 0, 0, 0, 0, 0, 0)
    return bytes(data)


class PreludeSplit(unittest.TestCase):
    def test_prelude_and_ta_of_render_compute_jobs(self):
        b = load()
        log = [present(0)]
        log += job(1, 2, 0, 5, start=1, end=5)                      # calibration
        log += job(2, 1, 5, 20, start=1, end=19, ta_end=17)          # rc_big
        log += [prelude(2, 7, 12)]
        log += job(3, 1, 20, 50, start=1, end=49, ta_end=25)         # main
        log += [prelude(3, 21, 22)]
        log += job(4, 1, 50, 60, start=1, end=59, ta_end=58)         # rc_big, no row
        log += job(5, 1, 60, 70, start=1, end=69, ta_end=68)         # rc_big, stale
        log += [prelude(5, 40, 41)]
        log += [present(100)]
        frames, _ = b.analyze(log, skip=0)
        f = frames[0]
        self.assertEqual(f['rc_big_jobs'], 3)
        self.assertEqual(f['rc_prelude_jobs'], 1)
        self.assertAlmostEqual(f['rc_prelude_ms'], 5)
        self.assertAlmostEqual(f['rc_prelude_kick_ms'], 2)
        self.assertAlmostEqual(f['rc_ta_ms'], 5)
        self.assertAlmostEqual(f['prelude_ms'], 5 + 1)

    def test_logs_without_prelude_rows(self):
        b = load()
        log = [present(0)] + job(1, 1, 10, 22, start=3, end=20, ta_end=15) + [present(100)]
        frames, _ = b.analyze(log, skip=0)
        self.assertEqual(frames[0]['rc_prelude_jobs'], 0)
        self.assertEqual(frames[0]['rc_prelude_ms'], 0)
        self.assertEqual(frames[0]['rc_ta_ms'], 0)


class KmdJobTiming(unittest.TestCase):
    def test_intervals_and_queued_gap(self):
        b = load()
        first = dict(submit=100, worker=110, begin_before=111, begin_after=190, backend_before=191,
                     backend_after=260, first_progress=600, complete=680, job_end=700, notify=745)
        # submitted while the first ran: waited for its notify
        second = dict(submit=300, worker=800, begin_before=801, begin_after=880, backend_before=881,
                      backend_after=950, first_progress=1290, complete=1370, notify=1430)
        # submitted after the second's notify: an idle GPU, not a KMD gap
        third = dict(submit=2000, worker=2050, backend_after=2200, complete=2500, notify=2560)
        out = b.kmd_job_timing(kmd_snapshot([(2, 7, second), (1, 7, first), (3, 7, third)]))
        self.assertEqual(out['slots'], 3)
        m = out['median_us']
        self.assertEqual(m['begin_job'], 79)
        self.assertEqual(m['build'], 69)
        self.assertEqual(m['kick_to_progress'], 340)
        self.assertEqual(m['complete_to_notify'], 60)
        self.assertEqual(out['samples']['queued_notify_to_worker'], 1)
        self.assertEqual(m['queued_notify_to_worker'], 800 - 745)
        self.assertEqual(m['queued_complete_to_kick'], 950 - 680)

    def test_refuses_another_layout(self):
        b = load()
        with self.assertRaises(ValueError):
            b.kmd_job_timing(kmd_snapshot([], version=2))
        with self.assertRaises(ValueError):
            b.kmd_job_timing(kmd_snapshot([])[:100])


if __name__ == '__main__':
    unittest.main()
