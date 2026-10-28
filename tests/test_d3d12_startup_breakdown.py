"""RC85: GravityMark start-up breakdown (tools/agx_startup_breakdown.py).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The tool decides where the scored first-frame stall goes, so its arithmetic
must be exact on the receipt ABI it reads:
- the stall is the largest gap between consecutive presents of the app (the
  process with the most presents), never another process's receipts;
- closing-receipt attribution: on the present thread the buckets add up to
  the wall time of the window exactly; another thread is charged only from
  its first receipt inside the window (its earlier idle time is unknown and
  must not inflate a bucket: the first draft charged 11.5 s of an idle loader
  thread to "pso" in gm2015a);
- measure-bo-create {MiB, class, allocation us, bind us, association us}
  splits its closing interval into bo-bind-paging and bo-allocate, capped by
  the interval (a receipt can never create more time than elapsed);
- RC85 measure-gpuva-map-time {MiB, residency us, Map us} charges that much
  of its interval to the bind and the rest (the allocation before it) to
  bo-allocate; measure-gpuva-paging-wait is bind time;
- measure-d3d12-wait packs (phase << 8 | site, count, us) triplets at words
  4..15 and measure-d3d12-replay-time six per-kind us then six counts, in
  the agx_win32_wait.h / ReplayTimingBucket order.
With the EXP2015 evidence present it also pins the finding: in gm2015b one
129 MiB BO bind takes more than 25 s of the 43 s stall.
"""
import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('agx_startup_breakdown', ROOT / 'tools/agx_startup_breakdown.py')
tool = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(tool)

HZ = tool.QPC_HZ
EVIDENCE = Path('/Volumes/pwdev/public_windows/.local/experiments/EXP2015-bulk-reads/evidence/EXP2015')


def line(name, seconds, *values, tid=10, pid=1, hr=0):
    words = ' '.join('%08x' % v for v in values)
    return 'measure-%s hr=0x%08x pid=%d tid=%d session=1 qpc=%d %s\n' % (
        name, hr, pid, tid, int(seconds * HZ), words)


def presents(times, tid=10, pid=1):
    return [line('d3d12-present', t, 0, 1, tid=tid, pid=pid) for t in times]


class StartupBreakdown(unittest.TestCase):
    def test_stall_is_the_largest_present_gap_of_the_app(self):
        lines = presents([1.0, 1.1, 1.2, 5.0, 5.1])
        # another process presents more rarely, with a bigger gap: ignored
        lines += presents([0.5, 20.0], tid=99, pid=2)
        result = tool.report(sorted(lines, key=lambda l: int(l.split('qpc=')[1].split()[0])))
        self.assertEqual(result['pid'], 1)
        self.assertEqual(result['stall_present'], 3)
        self.assertAlmostEqual(result['stall']['seconds'], 3.8, places=6)

    def test_times_frames_align_to_presents_and_load_gap_is_not_scored(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        rc93 (EXP2028 D3D12 HD 50k): the largest present gap (14.8 s,
        present 29 -> 30) was the app's scene load before frame 0, yet the
        tool called it the frame 1 -> 2 stall, and the task took the 44 s
        load for scored time. GravityMark scores frames in a fixed window
        from frame 0. With a times file the tool must find which present is
        frame 0 from the gap sequence, mark a load gap as outside the
        window, report the frame 1 -> 2 and 2 -> 3 phases from the presents
        that bound them, and size the scored loss against the median frame.
        """
        load = [1.0, 1.1, 9.0, 9.1, 9.2]          # 7.9 s load gap before frame 0
        frame0 = 9.3
        steady = [frame0 + 0.02, frame0 + 2.02] + [frame0 + 2.02 + 0.02 * i for i in range(1, 40)]
        lines = presents(load + [frame0] + steady)
        lines.append(line('bo-create', frame0 + 1.0, 128, 1, 900000, 100000, 0))
        lines.sort(key=lambda l: int(l.split('qpc=')[1].split()[0]))
        t0 = 30.0
        ts = [frame0] + steady
        times = ['%6d: %12.6f %9.6f 0\n' % (i, t0 + t - frame0, (t - ts[i - 1]) if i else 0.0)
                 for i, t in enumerate(ts)]
        result = tool.report(lines, times=times, window_frames=5)
        self.assertEqual(result['stall_present'], 2)
        self.assertEqual(result['frame0_present'], 5)
        self.assertFalse(result['stall_in_scored_window'])
        self.assertAlmostEqual(result['pre_window']['seconds'], frame0 - 1.0, places=5)
        f12 = result['frame_1_2']
        self.assertAlmostEqual(f12['seconds'], 2.0, places=5)
        self.assertAlmostEqual(f12['buckets']['bo-allocate']['s'], 0.88, places=3)  # interval 0.98 - bind 0.1
        self.assertAlmostEqual(result['frame_2_3']['seconds'], 0.02, places=5)
        stall = result['scored_stall']
        self.assertAlmostEqual(stall['median_frame_s'], 0.02, places=5)
        self.assertAlmostEqual(stall['excess_s'], 1.98, places=3)
        self.assertEqual(stall['lost_frames'], 99)

    def test_present_thread_buckets_add_up_to_the_wall_time(self):
        lines = presents([1.0])
        lines += [line('d3d12-shader-stats', 2.0, 0, 0, 5),          # 1.0 s compile
                  line('d3d12-staged-upload', 2.5, 0x00100010, 0, 0),  # 0.5 s upload
                  line('trim', 3.0, 1, 0x100000, 0, 0x100000, 0),      # 0.5 s residency
                  line('d3d12-pso', 7.0, tid=20),                       # idle loader thread
                  line('d3d12-pso', 7.25, tid=20)]                      # 0.25 s of it
        lines += presents([4.0])
        window = tool.analyse(tool.parse(lines), int(1.0 * HZ), int(4.0 * HZ))
        self.assertEqual(window['primary_tid'], 10)
        self.assertEqual(sum(window['buckets'].values()), int(4.0 * HZ) - int(1.0 * HZ))
        self.assertEqual(window['buckets']['shader-compile'], HZ)
        self.assertEqual(window['buckets']['upload'], HZ // 2)
        self.assertEqual(window['buckets']['residency'], HZ // 2)
        self.assertEqual(window['buckets']['gpu'], HZ)  # the closing present
        # receipts at 7 s are outside the window; inside, the loader is charged nothing before its first receipt
        lines2 = presents([1.0]) + [line('d3d12-pso', 3.0, tid=20), line('d3d12-pso', 3.25, tid=20)] + presents([4.0])
        window = tool.analyse(tool.parse(lines2), int(1.0 * HZ), int(4.0 * HZ))
        self.assertEqual(window['other_buckets']['pso'], HZ // 4)
        self.assertEqual(window['pso']['ticks'], 0)  # per-kind times are present-thread wall time

    def test_rc103_scene_escape_and_level_upload_receipts(self):
        """rc103 (EXP2037a): the 4 slow scene ACQUIREs of frame 1 -> 2 had no
        receipt, so the next receipt (measure-touch) was charged 1.29 s of
        residency. measure-scene-escape closes its own interval as an
        escape; measure-d3d12-level-upload is an upload and is counted."""
        lines = presents([1.0])
        lines += [line('scene-escape', 2.3, 1, 1_300_000, 4096, 4096, 0x2010, 0x101, 0),  # 1.3 s escape
                  line('touch', 2.31, 0x89090000, 1),                                    # 0.01 s residency
                  line('d3d12-level-upload', 2.5, 0x04000400, 0, 1024, 0, 0)]             # 0.19 s upload
        lines += presents([3.0])
        window = tool.analyse(tool.parse(lines), int(1.0 * HZ), int(3.0 * HZ))
        self.assertEqual(window['buckets']['escape'], int(2.3 * HZ) - int(1.0 * HZ))
        self.assertEqual(window['buckets']['residency'], int(2.31 * HZ) - int(2.3 * HZ))
        self.assertEqual(window['buckets']['upload'], int(2.5 * HZ) - int(2.31 * HZ))
        self.assertEqual(window['uploads']['level'], 1)

    def test_bo_create_split_is_capped_by_its_interval(self):
        lines = presents([1.0]) + [line('bo-create', 31.0, 129, 1, 700_000, 25_000_000, 4_000)] + presents([32.0])
        window = tool.analyse(tool.parse(lines), int(1.0 * HZ), int(32.0 * HZ))
        self.assertEqual(window['buckets']['bo-bind-paging'], 25 * HZ)
        self.assertEqual(window['buckets']['bo-allocate'], 7 * HZ // 10)
        self.assertEqual(window['buckets']['resource'], 30 * HZ - 25 * HZ - 7 * HZ // 10)
        # a 2 s interval cannot hold a 25 s bind
        capped = tool.split_bo_create(2 * HZ, [129, 1, 700_000, 25_000_000, 0])
        self.assertEqual(capped, {'bo-bind-paging': 2 * HZ, 'bo-allocate': 0, 'resource': 0})

    def test_rc85_bind_split_receipts(self):
        # allocation 1 s, residency (MakeResident + its paging wait) 22 s + Map 1 s, bind paging wait 3 s
        lines = presents([1.0]) + [
            line('gpuva-map-time', 25.0, 129, 22_000_000, 1_000_000),
            line('gpuva-paging-wait', 28.0, 3_000_000, 1, 77),
            line('bo-create', 28.5, 129, 1, 1_000_000, 26_000_000, 4_000)] + presents([29.0])
        window = tool.analyse(tool.parse(lines), int(1.0 * HZ), int(29.0 * HZ))
        self.assertEqual(window['buckets']['bo-allocate'], 24 * HZ - 23 * HZ)
        self.assertEqual(window['buckets']['bo-bind-paging'], 23 * HZ + 3 * HZ + HZ // 2)
        bind = window['binds'][0]
        self.assertEqual((bind['mib'], bind['residency_s'], bind['map_s'], bind['bind_wait_s']), (129, 22.0, 1.0, 3.0))

    def test_execute_wait_and_replay_layouts(self):
        wait = [1, 7, 0, 0,
                (6 << 8) | 17, 9994, 1_526_000,   # TransferMap in the submit phase
                2, 52, 942_000,                    # PagingFence
                0, 0, 0, 0, 0, 0]
        replay = [1_411_000, 236_000, 2_327_000, 1_267_000, 0, 69_000, 16, 165, 5284, 9, 0, 37]
        lines = presents([1.0]) + [line('d3d12-wait', 2.0, *wait), line('d3d12-execute', 2.1, 1, 5511, 5_311_000,
                                                                          191_000, 7),
                                   line('d3d12-replay-time', 2.2, *replay)] + presents([6.0])
        summary = tool.summarise(tool.analyse(tool.parse(lines), int(1.0 * HZ), int(6.0 * HZ)), 5)
        self.assertEqual(summary['waits_us']['TransferMap'], {'count': 9994, 's': 1.526})
        self.assertEqual(summary['waits_us']['PagingFence'], {'count': 52, 's': 0.942})
        self.assertEqual(summary['execute']['count'], 1)
        self.assertEqual(summary['execute']['replay_s'], 5.311)
        self.assertEqual(summary['execute']['replay_by_kind']['copy'], {'count': 5284, 's': 2.327})
        self.assertEqual(summary['execute']['replay_by_kind']['query'], {'count': 9, 's': 1.267})

    def test_malformed_lines_are_ignored(self):
        lines = ['garbage\n', 'measure-x hr=0x0 pid=1\n'] + presents([1.0, 2.0])
        self.assertEqual(len(tool.parse(lines)), 2)

    @unittest.skipUnless((EVIDENCE / 'gm2015b-umd.log').exists(), 'EXP2015 evidence not present')
    def test_gm2015b_stall_is_one_bo_bind(self):
        with open(EVIDENCE / 'gm2015b-umd.log', errors='replace') as f:
            result = tool.report(f)
        stall = result['stall']
        self.assertGreater(stall['seconds'], 40)
        top = stall['bo_creates'][0]
        self.assertEqual((top['mib'], top['class']), (129, 1))
        self.assertGreater(top['bind_s'], 25)
        self.assertGreater(stall['buckets']['bo-bind-paging']['pct'], 50)


if __name__ == '__main__':
    unittest.main()
