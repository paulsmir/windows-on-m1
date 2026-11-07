"""An ordinary launch must not inherit a native ownership switch from its shell."""
import contextlib
import io
import os
from pathlib import Path
import runpy
import subprocess
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]

class AnsLauncherModeTests(unittest.TestCase):
    def test_recovery_dry_run_clears_inherited_native_mode(self):
        with patch.dict(os.environ, {'WOM1_STORAGE_MODE': 'ANS_NATIVE'}):
            with patch.object(sys, 'argv', ['run_uefi.py', '--dry-run']):
                with contextlib.redirect_stdout(io.StringIO()):
                    with self.assertRaises(SystemExit) as stop:
                        runpy.run_path(str(ROOT/'run_uefi.py'), run_name='__main__')
            self.assertEqual(stop.exception.code, 0)
            self.assertEqual(os.environ['WOM1_STORAGE_MODE'], 'VNVME_RECOVERY')

    def test_native_is_rejected_before_uart_until_candidate_admission(self):
        p = subprocess.run([sys.executable, str(ROOT/'run_uefi.py'),
                            '--storage-mode', 'ANS_NATIVE', '--dry-run'],
                           cwd=ROOT, capture_output=True, text=True)
        self.assertNotEqual(p.returncode, 0)
        self.assertIn('native qualification candidate is not yet admitted', p.stderr)
        self.assertNotIn('UartInterface', p.stderr)

    def test_ans_direct_requires_native_mode(self):
        p = subprocess.run([sys.executable, str(ROOT/'run_uefi.py'), '--ans-direct', '--dry-run'],
                           cwd=ROOT, capture_output=True, text=True)
        self.assertNotEqual(p.returncode, 0)
        self.assertIn('--ans-direct requires explicit --storage-mode ANS_NATIVE', p.stderr)

    def test_recovery_dry_run_clears_inherited_direct_mode(self):
        with patch.dict(os.environ, {'WOM1_ANS_DIRECT': '1'}):
            with patch.object(sys, 'argv', ['run_uefi.py', '--dry-run']):
                with contextlib.redirect_stdout(io.StringIO()):
                    with self.assertRaises(SystemExit) as stop:
                        runpy.run_path(str(ROOT/'run_uefi.py'), run_name='__main__')
            self.assertEqual(stop.exception.code, 0)
            self.assertNotIn('WOM1_ANS_DIRECT', os.environ)

    def test_native_direct_is_still_gated_by_candidate_admission(self):
        p = subprocess.run([sys.executable, str(ROOT/'run_uefi.py'), '--storage-mode', 'ANS_NATIVE',
                            '--ans-direct', '--dry-run'], cwd=ROOT, capture_output=True, text=True)
        self.assertNotEqual(p.returncode, 0)
        self.assertIn('native qualification candidate is not yet admitted', p.stderr)

    def test_assisted_script_rejects_direct_without_native(self):
        p = subprocess.run(['sh', str(ROOT/'scripts/run-assisted.sh'), '--ans-direct', '--dry-run'],
                           cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(p.returncode, 2)
        self.assertIn('--ans-direct requires explicit ANS_NATIVE mode', p.stderr)

    def test_assisted_script_forwards_direct_on_both_native_paths(self):
        script = (ROOT/'scripts/run-assisted.sh').read_text()
        self.assertIn('--ans-direct) ANS_DIRECT=1; shift ;;', script)
        self.assertEqual(script.count('[ "$ANS_DIRECT" -eq 0 ] || set -- "$@" --ans-direct'), 2)


class AnsDirectHypervisorArmingTests(unittest.TestCase):
    """Execute the actual HV.map_ans_native_broker with a fake proxy (no construct/USB)."""

    def load(self):
        import ast
        path = ROOT/'m1n1_windows/proxyclient/m1n1/hv/__init__.py'
        tree = ast.parse(path.read_text())
        node = next(n for c in tree.body if isinstance(c, ast.ClassDef) and c.name == 'HV'
                    for n in c.body if isinstance(n, ast.FunctionDef) and n.name == 'map_ans_native_broker')
        module = ast.Module(body=[node], type_ignores=[])
        namespace = {'os': os, '__file__': str(path),
                     'irange': lambda base, size: (base, size),
                     'TraceMode': type('TraceMode', (), {'RESERVED': 'reserved'})}
        exec(compile(module, str(path), 'exec'), namespace)
        return namespace['map_ans_native_broker']

    def fake(self, direct_ok=True):
        calls = []
        class Proxy:
            def hv_map_ans_broker(self, *a): calls.append(('map',) + a); return True
            def hv_ans_write_window(self, *a): calls.append(('window',) + a); return True
            def hv_ans_direct(self, *a): calls.append(('direct',) + a); return direct_ok
        class Node:
            def get_reg(self, index): return (0x1000 * (index + 1), 0x1000)
        class Fake:
            p = Proxy()
            adt = {'/arm-io/ans': Node()}
            def add_tracer(self, *a): calls.append(('tracer',) + a)
        return Fake(), calls

    def run_mode(self, env, direct_ok=True):
        method = self.load()
        hv, calls = self.fake(direct_ok)
        base = {'WOM1_STORAGE_MODE': 'ANS_NATIVE'}
        base.update(env)
        with patch.dict(os.environ, base, clear=False):
            for key in ('WOM1_ANS_WRITE_WINDOW', 'WOM1_ANS_DIRECT'):
                if key not in env:
                    os.environ.pop(key, None)
            with contextlib.redirect_stdout(io.StringIO()):
                method(hv)
        return calls

    def test_direct_arms_el2_once_after_broker_mapping(self):
        calls = self.run_mode({'WOM1_ANS_DIRECT': '1'})
        ops = [c[0] for c in calls if c[0] != 'tracer']
        self.assertEqual(ops, ['map', 'direct'])

    def test_direct_and_window_are_exclusive(self):
        with self.assertRaisesRegex(ValueError, 'exclusive'):
            self.run_mode({'WOM1_ANS_DIRECT': '1', 'WOM1_ANS_WRITE_WINDOW': '0:8'})

    def test_direct_value_is_strict_and_rejection_is_fatal(self):
        with self.assertRaisesRegex(ValueError, 'must be 1'):
            self.run_mode({'WOM1_ANS_DIRECT': 'yes'})
        with self.assertRaisesRegex(RuntimeError, 'direct mode rejected'):
            self.run_mode({'WOM1_ANS_DIRECT': '1'}, direct_ok=False)

    def test_window_path_unchanged_without_direct(self):
        calls = self.run_mode({'WOM1_ANS_WRITE_WINDOW': '0:8'})
        ops = [c[0] for c in calls if c[0] != 'tracer']
        self.assertEqual(ops, ['map', 'window'])


if __name__ == '__main__':
    unittest.main()
