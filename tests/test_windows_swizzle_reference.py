"""Original CPU0004 subset oracle; deliberately no production caps or GPU."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class WindowsSwizzleReference(unittest.TestCase):
    def test_hand_derived_vectors_and_rejected_ambiguous_inputs(self):
        with tempfile.TemporaryDirectory(prefix='windows-swizzle-reference-') as tmp:
            exe=Path(tmp)/'reference'
            r=subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(ROOT/'tests/windows_swizzle_reference_fixture.c'),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)
            r=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
