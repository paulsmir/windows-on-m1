"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The iteration planner decides what may be skipped. It must never send a KMD
or m1n1 change down the no-reboot UMD hot-swap path, never let a requested
FAST run stand in for the PERF/FULL rule of a KMD change, must mark every
speed claim as needing a FULL benchmark, and must not select tests through
short names such as the dirty `mu` gitlink (which matched 474 modules).
"""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import agx_iterate as it  # noqa: E402

KMD = 'drivers/apple-agx/render-admission/src/backend_platform_windows.c'
UMD = 'drivers/apple-agx/mesa/scripts/native-asahi-latency-schedule.py'


class Iterate(unittest.TestCase):
    def test_components(self):
        self.assertEqual(it.component(KMD), 'kmd')
        self.assertEqual(it.component('drivers/apple-agx/shared/src/apple_agx_render_shared_memory.c'), 'kmd')
        self.assertEqual(it.component(UMD), 'umd')
        self.assertEqual(it.component('drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp'), 'umd')
        self.assertEqual(it.component('m1n1_windows'), 'm1n1')
        self.assertEqual(it.component('investigation/EXPERIMENTS.md'), 'docs')
        self.assertEqual(it.component('tools/agx_gm_perf.py'), 'tools')

    def test_umd_only_uses_hot_swap_without_reboot(self):
        p = it.plan(files=[UMD], hypothesis='perf')
        self.assertEqual([h['path'] for h in p['hardware']], ['umd-hotswap'])
        self.assertFalse(p['hardware'][0]['reboot'])
        self.assertEqual(p['gravitymark_mode'], 'PERF')
        self.assertTrue(p['full_benchmark_required'])
        self.assertIn('KMD package + reboot', [s['check'] for s in p['skipped']])

    def test_kmd_change_needs_the_package_path_and_cannot_downgrade(self):
        p = it.plan(files=[KMD, UMD], hypothesis='perf', mode='FAST')
        self.assertEqual(sorted(h['path'] for h in p['hardware']), ['package', 'umd-hotswap'])
        self.assertTrue(all(h['mode'] == 'PERF' for h in p['hardware']))
        self.assertIsNotNone(p['mode_refused'])
        self.assertTrue(p['full_benchmark_required'])
        c = it.plan(files=[KMD], hypothesis='correctness')
        self.assertEqual(c['gravitymark_mode'], 'FAST')
        self.assertTrue(c['full_benchmark_required'], 'a KMD change is confirmed by FULL')
        d = it.plan(files=[KMD], hypothesis='default')
        self.assertEqual(d['gravitymark_mode'], 'FULL')

    def test_m1n1_needs_permission_step(self):
        p = it.plan(files=['m1n1_windows'], hypothesis='correctness')
        self.assertEqual(p['hardware'][0]['path'], 'm1n1')
        self.assertIn('permission', p['hardware'][0]['steps'][0])

    def test_docs_and_tests_skip_hardware(self):
        p = it.plan(files=['investigation/EXPERIMENTS.md'])
        self.assertEqual(p['hardware'], [])
        self.assertFalse(p['full_benchmark_required'])
        self.assertEqual(p['offline_tests'], [])
        self.assertIn('hardware', [s['check'] for s in p['skipped']])
        t = it.plan(files=['tests/test_agx_gm_perf.py'])
        self.assertEqual(t['offline_tests'], ['test_agx_gm_perf'])
        self.assertTrue(t['baseline_suite'])

    def test_short_names_do_not_select_everything(self):
        self.assertEqual(it.select_tests(['mu']), {})
        chosen = it.select_tests(['tools/agx_gravitymark_run.ps1'])
        self.assertIn('test_d3d12_gravitymark_run', chosen)
        self.assertLess(len(chosen), 10)

    def test_failure_ids_match_the_baseline_runner(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            Path(tmp, 'test_zz_probe.py').write_text(
                'import unittest\nclass P(unittest.TestCase):\n'
                '    def test_ok(self): pass\n    def test_bad(self): self.fail()\n')
            Path(tmp, 'test_zz_broken.py').write_text('import does_not_exist\n')
            self.assertEqual(it.failing(tmp, ['test_zz_probe', 'test_zz_broken']),
                             {'test_zz_probe.P.test_bad', 'test_zz_broken'})


if __name__ == '__main__':
    unittest.main()
