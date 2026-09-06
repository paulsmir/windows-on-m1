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

if __name__ == '__main__':
    unittest.main()
