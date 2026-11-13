"""The commit gate must never report BASELINE_OK for a run that did not happen.

2026-11-13: `python -m unittest a b` turned only an ImportError into a failed
test; any other exception at import aborted the whole run and
run_against_baseline.py still printed BASELINE_OK.  Each case runs the real
gate script, copied into a temporary directory with probe modules.
"""
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

GATE = Path(__file__).resolve().parent / 'run_against_baseline.py'
OK = 'import unittest\nclass T(unittest.TestCase):\n    def test_ok(self):\n        pass\n'


class GateRunnerIntegrity(unittest.TestCase):
    def gate(self, modules, files):
        with tempfile.TemporaryDirectory() as directory:
            here = Path(directory)
            shutil.copy(GATE, here / GATE.name)
            for name, text in files.items():
                (here / f'{name}.py').write_text(text)
            run = subprocess.run([sys.executable, GATE.name, *modules], cwd=here,
                                 capture_output=True, text=True, timeout=120)
        return run.returncode, run.stdout

    def test_exception_at_import_is_a_new_failure_and_others_still_run(self):
        rc, out = self.gate(['test_p_*'], {'test_p_bad': 'raise RuntimeError("import")\n',
                                           'test_p_good': OK})
        self.assertEqual(rc, 1)
        self.assertIn('NEW test_p_bad', out)
        self.assertIn('ran 1 tests', out)
        self.assertNotIn('BASELINE_OK', out)

    def test_dead_runner_is_incomplete_not_ok(self):
        rc, out = self.gate(['test_p_*'], {'test_p_exit': 'import os\nos._exit(3)\n',
                                           'test_p_good': OK})
        self.assertEqual(rc, 1)
        self.assertIn('GATE_INCOMPLETE', out)
        self.assertNotIn('BASELINE_OK', out)

    def test_clean_run_is_ok(self):
        rc, out = self.gate(['test_p_good'], {'test_p_good': OK})
        self.assertEqual(rc, 0)
        self.assertIn('BASELINE_OK', out)


if __name__ == '__main__':
    unittest.main()
