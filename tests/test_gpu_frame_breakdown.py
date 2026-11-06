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
whose prelude stamps are stale, contribute nothing to the split. The
measure-gpu-tvb rows carry the process manager's cumulative TVB overflow
count: a render's partial renders are the increase since the process's
previous render with a row (none for the first one or after a decrease).
rc81: the v2 receipt (rc81 render_job_timing.h) adds the worker's processor
and efficiency class at the build and at completion, the BeginJob flags and
the header's worker flags; the decoder splits the CPU-bound intervals by
job kind (a render has a TA kick) and by the class of the processor that ran
them, counts the BeginJobs that found the mapping generation unchanged, and
still reads v1 snapshots.
rc94 (--jobs): per-position medians use only the frames of the most common
job sequence; a job is late when the KMD received it no earlier than the
previous own job's completion and foreign when another process's completion
took a sequence number in between.
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


def tvb(seq, overflows, max_pages, pid=7):
    return row('measure-gpu-tvb', [seq, 0, overflows, max_pages], pid)


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
        # rc76: job 2 starts 3 ms after job 1's firmware end (2 detection +
        # 1 after the completion; EXP2215 bound), job 3 2 ms after its submit
        self.assertEqual(f['c_kick_jobs'], 2)
        self.assertAlmostEqual(f['c_kick_ms'], 3 + 2)
        self.assertAlmostEqual(f['r_small_ta_ms'], 0.3)
        g = frames[1]
        self.assertEqual(g['jobs'], 2)
        self.assertAlmostEqual(g['gpu_span_ms'], 19 + 5)
        self.assertAlmostEqual(g['main_3d_ms'], 9)
        self.assertEqual(g['c_empty_jobs'], 1)
        self.assertEqual(g['c_work_jobs'], 0)
        self.assertAlmostEqual(g['idle_ms'], 100 - 20 - 5)
        self.assertEqual(g['c_kick_jobs'], 0)  # job 8's start is stale

    def test_positions_late_and_foreign_jobs(self):
        b = load()
        log = [present(0)]
        log += job(1, 2, 1, 5, start=2, end=5)                         # C: launch 1
        log += job(2, 1, 3, 20, end=20, ta_end=12)                     # R queued: lo 5
        log += [prelude(2, 6, 7)]
        log += [present(30)]
        log += job(3, 2, 15, 35, start=21.5, end=35)                   # C queued: lo 20, launch 1.5
        log += job(5, 1, 36, 50, end=50, ta_end=40)                    # R late, seq 4 foreign
        log += [prelude(5, 38, 39)]
        log += [present(60)]
        log += job(6, 1, 61, 70, end=70, ta_end=65)                    # another sequence (R only)
        log += [present(90)]
        out = b.positions(log, skip=0)
        self.assertEqual((out['signature'], out['frames'], out['of_frames']), ('CR', 2, 3))
        c, r = out['positions']
        self.assertEqual((c['kind'], r['kind']), ('C', 'R'))
        self.assertAlmostEqual(c['launch'], (1 + 1.5) / 2)
        self.assertAlmostEqual(r['launch'], (1 + 2) / 2)                # 6 - 5, 38 - 36
        self.assertAlmostEqual(r['late'], 0.5)
        self.assertAlmostEqual(r['foreign'], 0.5)
        self.assertAlmostEqual(c['late'], 0.0)
        self.assertAlmostEqual(r['to_ta'], (7 + 4) / 2)

    def test_depth2_starts_before_the_previous_completion(self):
        # EXP2215: a job kicked while its predecessor ran starts at the
        # predecessor's firmware end, before the KMD saw that completion.
        # Bounded by the completion (lo) the early start was dropped (the
        # prelude) or clamped to 0 (the compute kick).
        b = load()
        log = [present(0)]
        log += job(1, 2, 0, 12, start=1, end=10)                       # C, detected 2 ms late
        log += job(2, 1, 3, 30, end=30, ta_end=20)                     # R kicked at depth 2
        log += [prelude(2, 10.1, 10.2)]
        log += job(3, 2, 4, 33, start=30.1, end=31)                    # C kicked at depth 2
        log += job(4, 1, 40, 50, end=50, ta_end=45)                    # R submitted late
        log += [prelude(4, 41, 41.5)]
        log += job(5, 1, 41, 60, end=60, ta_end=55)                    # stale prelude row
        log += [prelude(5, 20, 20.5)]
        log += [present(100)]
        jobs = b.timeline(b.parse(log)[2], 10000)
        r2, c3, r4, r5 = jobs[1], jobs[2], jobs[3], jobs[4]
        self.assertAlmostEqual(r2['prelude_kick'], 0.1)
        self.assertAlmostEqual(r2['prelude'], 0.1)
        self.assertAlmostEqual(c3['kick'], 0.1)
        self.assertAlmostEqual(r4['prelude_kick'], 1)                  # from its submit
        self.assertIsNone(r5['prelude'])                               # before job 4's end

    def test_failed_job_has_no_firmware_time(self):
        b = load()
        log = [present(0)] + job(1, 1, 10, 20, status=1) + job(2, 2, 20, 30, start=21, end=30)
        log += [present(100)]
        frames, summary = b.analyze(log, skip=0)
        self.assertEqual(summary['failed_jobs'], 1)
        self.assertAlmostEqual(frames[0]['detect_ms'], 0)
        self.assertAlmostEqual(frames[0]['gpu_span_ms'], 10 + 10)


def kmd_snapshot(slots, hz=1000000, version=1, worker_flags=0, size=None):
    """slots: (fence, pid, {phase: qpc}[, (cpu_build, cpu_complete, flags[, ahead])[, {sub: qpc}]])"""
    b = load()
    slot_bytes = {1: 168, 2: 184, 3: 248}.get(version, 288)
    data = bytearray(struct.pack('<IIIIQQ', version, size or 32 + 64 * slot_bytes, len(slots),
                                 worker_flags, hz, 0))
    for i in range(64):
        fence, pid, q, *extra = slots[i] if i < len(slots) else (0, 0, {})
        data += struct.pack('<IIIIQQ12Q4QII', fence, pid, 0, 0, 0, 0,
                            *[q.get(p, 0) for p in b.KMD_PHASES], 0, 0, 0, 0, 0, 0)
        if version != 1:
            words = tuple(extra[0]) if extra else (0, 0, 0)
            data += struct.pack('<IIII', *(words + (0,) * (4 - len(words))))
        if version >= 3:
            sub = extra[1] if len(extra) > 1 else {}
            names = b.KMD_SUBS_V4 if version >= 4 else b.KMD_SUBS
            data += struct.pack('<%dQ' % len(names), *[sub.get(n, 0) for n in names])
    return bytes(data)


def cpu(index, efficiency):
    return index | efficiency << 8 | 1 << 31


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


class TvbPartialRenders(unittest.TestCase):
    def test_partial_renders_per_render_from_cumulative_overflows(self):
        b = load()
        log = [present(0)]
        log += job(1, 1, 0, 10, start=1, end=9, ta_end=8) + [tvb(1, 100, 900)]   # first: unknown
        log += job(2, 2, 10, 12, start=10.5, end=12)                            # compute
        log += job(3, 1, 12, 40, start=1, end=39, ta_end=20) + [tvb(3, 103, 950)] # main, 3
        log += job(4, 1, 40, 50, start=1, end=49, ta_end=48) + [tvb(4, 110, 960)] # rc_big, 7
        log += job(5, 1, 50, 52, start=1, end=51.5, ta_end=51) + [tvb(5, 2, 10)]  # reset
        log += [present(100)]
        frames, _ = b.analyze(log, skip=0)
        f = frames[0]
        self.assertEqual(f['rc_partials'], 7)
        self.assertEqual(f['main_partials'], 3)
        self.assertEqual(f['partials'], 10)
        self.assertEqual(f['partial_jobs'], 2)
        self.assertEqual(f['tvb_max_pages'], 960)


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

    def test_v3_build_ahead_flags_refusals_and_chained_gap(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        The rc90 hardware A/B reads Wom1JobBuildAhead's effect from this
        decoder: the chained/dispatched split of queued_complete_to_kick and
        the per-step refusal histograms must come from the slot flags and the
        AheadRefusals word (render_job_timing.h), not from the old Reserved
        zero."""
        b = load()
        first = dict(submit=100, worker=110, backend_before=191, backend_after=260,
                     first_progress=600, complete=680, job_end=700, notify=745)
        # chained: kicked 150 us after the first's completion
        second = dict(submit=300, worker=760, backend_before=761, backend_after=830,
                      first_progress=1200, complete=1300, notify=1360)
        # dispatched (preemption refused the chain): 400 us
        third = dict(submit=900, worker=1500, backend_before=1501, backend_after=1700,
                     first_progress=2000, complete=2100, notify=2160)
        released, prebound, chained = 0x100, 0x200, 0x400
        out = b.kmd_job_timing(kmd_snapshot([
            (1, 7, first, (0, 0, released, 0 | 0 << 8 | 0 << 16)),
            (2, 7, second, (0, 0, released | prebound | chained, 4 << 16)),
            (3, 7, third, (0, 0, released, 25 << 8))], version=3))
        m = out['median_us']
        self.assertEqual(m['queued_complete_to_kick_chained'], 830 - 680)
        self.assertEqual(m['queued_complete_to_kick_dispatched'], 1700 - 1300)
        self.assertEqual(out['ahead_flags'], dict(released=3, prebound=1, chained=1))
        self.assertEqual(out['ahead_refusals'], dict(release={}, prebind={25: 1}, chain={4: 1}))

    def test_v2_splits_kind_and_core_class(self):
        b = load()
        def render(fence, at, build, finish, begin=10):
            return dict(submit=at, worker=at + 1, begin_before=at + 2, begin_after=at + 2 + begin,
                        backend_before=at + 3 + begin, kick_ta=at + 3 + begin + build - 5,
                        kick_3d=at + 3 + begin + build - 5, backend_after=at + 3 + begin + build,
                        complete=at + 500, job_end=at + 500 + finish, notify=at + 540 + finish)
        def compute(at, build, finish, begin=10):
            return dict(submit=at, worker=at + 1, begin_before=at + 2, begin_after=at + 2 + begin,
                        backend_before=at + 3 + begin, backend_after=at + 3 + begin + build,
                        complete=at + 300, job_end=at + 300 + finish, notify=at + 306 + finish)
        slow, fast = 0, 1
        slots = [(1, 7, render(1, 1000, 265, 72, begin=40), (cpu(1, slow), cpu(1, slow), 7)),
                 (2, 7, render(2, 3000, 70, 19, begin=11), (cpu(5, fast), cpu(5, fast), 5)),
                 (3, 7, render(3, 5000, 72, 21, begin=12), (cpu(6, fast), cpu(2, slow), 4)),
                 (4, 7, compute(7000, 5, 8), (cpu(4, fast), cpu(4, fast), 4)),
                 (5, 7, compute(9000, 6, 30), (0, 0, 0))]
        out = b.kmd_job_timing(kmd_snapshot(slots, version=2, worker_flags=0x00040103))
        self.assertEqual(out['version'], 2)
        self.assertEqual(out['worker_flags'], dict(thread=True, pinned=True, efficiency_class=1, cores=4))
        kind = out['by_kind']
        self.assertEqual(kind['render']['samples']['build'], 3)
        self.assertEqual(kind['render']['median_us']['build'], 72)
        self.assertEqual(kind['render']['median_us']['complete_to_job_end'], 21)
        self.assertEqual(kind['compute']['median_us']['build'], 5.5)
        self.assertEqual(kind['compute']['median_us']['job_end_to_notify'], 6)
        cls = out['by_core_class']
        # build and BeginJob by the class at the build, completion by the class at completion
        self.assertEqual(cls['render'][1]['median_us']['build'], 71)
        self.assertEqual(cls['render'][0]['median_us']['build'], 265)
        self.assertEqual(cls['render'][0]['median_us']['begin_job'], 40)
        self.assertEqual(cls['render'][0]['median_us']['complete_to_job_end'], (72 + 21) / 2)
        self.assertEqual(cls['render'][1]['median_us']['complete_to_job_end'], 19)
        self.assertEqual(cls['compute'][1]['samples']['build'], 1)
        self.assertNotIn(0, cls['compute'])
        self.assertEqual(out['begin_flags'], dict(begin_jobs=4, mapping_unchanged=2, private_scene=1))

    def test_v1_snapshots_still_decode(self):
        b = load()
        first = dict(submit=100, worker=110, begin_before=111, begin_after=190, backend_before=191,
                     kick_ta=250, backend_after=260, complete=680, job_end=700, notify=745)
        out = b.kmd_job_timing(kmd_snapshot([(1, 7, first)]))
        self.assertEqual(out['version'], 1)
        self.assertEqual(out['by_kind']['render']['median_us']['build'], 69)
        self.assertNotIn('by_core_class', out)

    def test_refuses_another_layout(self):
        b = load()
        with self.assertRaises(ValueError):
            b.kmd_job_timing(kmd_snapshot([], version=2, size=32 + 64 * 168))
        with self.assertRaises(ValueError):
            b.kmd_job_timing(kmd_snapshot([], version=5))
        with self.assertRaises(ValueError):
            b.kmd_job_timing(kmd_snapshot([], version=3, size=32 + 64 * 184))
        with self.assertRaises(ValueError):
            b.kmd_job_timing(kmd_snapshot([])[:100])


if __name__ == '__main__':
    unittest.main()
