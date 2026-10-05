"""Paging publication must make local copies independent of scheduling."""
import os
from pathlib import Path
import subprocess
import sys
import unittest
ROOT = Path(__file__).resolve().parents[1]
class PagingRootOrderingTests(unittest.TestCase):
    def test_real_paging_copy_and_relocation_without_setroot(self):
        for profile in ('16', '64'):
            with self.subTest(profile=profile):
                run = subprocess.run([sys.executable, str(ROOT/'tests/g3_vidmm_replay.py')],
                    env=dict(os.environ, G3_REPLAY_R145='1', G3_REPLAY_R147='1',
                             G3_REPLAY_PROFILE=profile), cwd=ROOT, text=True, capture_output=True)
                self.assertEqual(run.returncode, 0, run.stdout+run.stderr)
