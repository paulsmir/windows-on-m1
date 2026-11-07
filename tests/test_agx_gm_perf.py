"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
A short GravityMark run is only trustworthy if its window starts after the
loading screen and shader-compile hitches (a window that starts on the first
present measured the loading screen: 22 fps instead of 37), if render jobs
with stale 3D starts are not counted as GPU busy time, and if the BEFORE/AFTER
verdict never calls a change inside the run-to-run noise an improvement.
"""
import json
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import agx_gm_perf as perf  # noqa: E402

HZ = 24_000_000


def line(tag, qpc, words, pid=7):
    return '%s hr=0x00000000 pid=%d tid=1 session=1 qpc=%d %s\n' % (
        tag, pid, qpc, ' '.join('%08x' % w for w in words))


def u64(value):
    return [value & 0xffffffff, value >> 32]


def trace(frame_ms=27.0, prelude_gap_ms=0.6, ta_starts=False, seconds=40):
    out = [line('measure-gpu-completion', 0, [0] * 11 + u64(HZ))]
    t = 1_000_000
    # loading screen: presents every 0.2 s, then a 5 s shader-compile hitch
    for _ in range(20):
        out.append(line('measure-d3d12-present', t, [0] * 9)); t += HZ // 5
    t += 5 * HZ
    fw, seq = 10 * HZ, 1
    for _ in range(int(seconds * 1000 / frame_ms)):
        out.append(line('measure-d3d12-present', t, [0] * 9)); t += int(frame_ms * HZ / 1000)
        # compute job (valid start), then a prelude render after prelude_gap_ms
        c_start, c_end = fw, fw + HZ // 100
        out.append(line('measure-gpu-stages', 0, u64(seq) + u64(c_start) + u64(c_end) + [0] * 4))
        pre = c_end + int(prelude_gap_ms * HZ / 1000)
        ta_s, ta_e, d3_e = pre + HZ // 10000, pre + HZ // 1000, pre + HZ // 50
        stale = 5  # G13 3D starts are written once (EXP1112)
        out.append(line('measure-gpu-stages', 0, u64(seq + 1) + u64(stale) + u64(d3_e) +
                        u64(ta_s if ta_starts else stale) + u64(ta_e)))
        out.append(line('measure-gpu-prelude', 0, u64(seq + 1) + u64(pre) + u64(pre + HZ // 10000)))
        fw, seq = d3_e + HZ // 2500, seq + 2
    return out


class GmPerf(unittest.TestCase):
    def test_window_starts_after_loading_and_compile_hitch(self):
        d = perf.summarize(trace(), skip=2, window=20)
        self.assertAlmostEqual(d['anchor_s'], 4.0 + 5.0, delta=0.3)
        self.assertAlmostEqual(d['frame_ms']['mean'], 27.0, delta=0.05)
        self.assertAlmostEqual(d['fps'], 37.04, delta=0.1)
        self.assertGreater(d['frames'], 700)
        self.assertAlmostEqual(d['gpu']['c_to_prelude_ms']['median'], 0.6, delta=0.01)
        self.assertAlmostEqual(d['gpu']['r_to_c_ms']['median'], 0.4, delta=0.01)

    def test_stale_render_starts_do_not_count_as_busy(self):
        stale = perf.summarize(trace(), skip=2, window=20)['gpu']
        self.assertIsNone(stale['busy_fraction'])
        self.assertIsNone(stale['c_to_ta_ms'])
        valid = perf.summarize(trace(ta_starts=True), skip=2, window=20)['gpu']
        self.assertGreater(valid['busy_fraction'], 0.9)
        self.assertLess(valid['busy_fraction'], 1.0)
        self.assertAlmostEqual(valid['c_to_ta_ms']['median'], 0.7, delta=0.01)

    def test_compare_needs_more_than_noise_and_paired_blocks(self):
        base = perf.summarize(trace(27.0), 2, 20)
        self.assertEqual(perf.compare(base, perf.summarize(trace(26.9), 2, 20))['verdict'],
                         'no-significant-change')
        self.assertEqual(perf.compare(base, perf.summarize(trace(26.0), 2, 20))['verdict'], 'faster')
        self.assertEqual(perf.compare(base, perf.summarize(trace(28.0), 2, 20))['verdict'], 'slower')
        other = dict(perf.summarize(trace(26.0), 2, 20)); other['window_s'] = 30
        self.assertEqual(perf.compare(base, other)['verdict'], 'invalid')
        removed = perf.summarize(trace(26.0), 2, 20, err_text='D3D12::error(): device removed')
        self.assertEqual(perf.compare(base, removed)['verdict'], 'invalid')
        mixed = dict(perf.summarize(trace(26.0), 2, 20))
        mixed['block_means_ms'] = [26.0, 28.0] * 4
        self.assertEqual(perf.compare(base, mixed)['verdict'], 'inconclusive')

    def test_cli_writes_machine_readable_json(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / 'umd.log'
            log.write_text(''.join(trace()))
            out = Path(tmp) / 'perf.json'
            run = subprocess.run([sys.executable, str(ROOT / 'tools/agx_gm_perf.py'), 'summary',
                                  str(log), '--skip', '2', '--window', '20', '--json', str(out)],
                                 capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertEqual(json.loads(out.read_text())['version'], 1)

    def test_too_short_window_is_refused(self):
        with self.assertRaises(ValueError):
            perf.summarize(trace(seconds=1), skip=0, window=0.1)


if __name__ == '__main__':
    unittest.main()
